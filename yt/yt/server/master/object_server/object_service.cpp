#include "object_service.h"
#include "private.h"
#include "object_manager.h"
#include "config.h"
#include "helpers.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/config.h>
#include <yt/yt/server/master/cell_master/config_manager.h>
#include <yt/yt/server/master/cell_master/hydra_facade.h>
#include <yt/yt/server/master/cell_master/master_hydra_service.h>
#include <yt/yt/server/master/cell_master/multi_phase_cell_sync_session.h>

#include <yt/yt/server/master/cypress_server/cypress_manager.h>
#include <yt/yt/server/master/cypress_server/resolve_cache.h>

#include <yt/yt/server/master/object_server/path_resolver.h>
#include <yt/yt/server/master/object_server/request_profiling_manager.h>

#include <yt/yt/server/master/security_server/security_manager.h>
#include <yt/yt/server/master/security_server/user.h>

#include <yt/yt/server/master/sequoia_server/config.h>

#include <yt/yt/server/master/transaction_server/transaction_replication_session.h>

#include <yt/yt/server/master/transaction_server/proto/transaction_manager.pb.h>

#include <yt/yt/server/lib/hive/hive_manager.h>

#include <yt/yt/server/lib/object_server/helpers.h>

#include <yt/yt/server/lib/transaction_server/helpers.h>

#include <yt/yt/server/lib/transaction_supervisor/transaction_supervisor.h>

#include <yt/yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/yt/ytlib/object_client/object_service_cache.h>
#include <yt/yt/ytlib/object_client/object_service_proxy.h>

#include <yt/yt/ytlib/transaction_client/helpers.h>

#include <yt/yt/ytlib/api/native/connection.h>

#include <yt/yt/ytlib/hive/cell_directory.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/hydra/public.h>

#include <yt/yt/core/rpc/helpers.h>
#include <yt/yt/core/rpc/message.h>
#include <yt/yt/core/rpc/service_detail.h>
#include <yt/yt/core/rpc/dispatcher.h>
#include <yt/yt/core/rpc/per_key_request_queue_provider.h>

#include <yt/yt/core/ytree/request_complexity_limiter.h>
#include <yt/yt/core/ytree/ypath_detail.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/misc/codicil.h>
#include <yt/yt/core/misc/fair_scheduler.h>
#include <yt/yt/core/misc/heap.h>
#include <yt/yt/core/misc/mpsc_stack.h>

#include <yt/yt/core/actions/cancelable_context.h>
#include <yt/yt/core/actions/signal.h>

#include <yt/yt/core/concurrency/quantized_executor.h>
#include <yt/yt/core/concurrency/thread_pool.h>
#include <yt/yt/core/concurrency/throughput_throttler.h>

#include <library/cpp/yt/threading/recursive_spin_lock.h>
#include <library/cpp/yt/threading/spin_lock.h>
#include <library/cpp/yt/threading/traceless_guard.h>

#include <library/cpp/yt/compact_containers/compact_vector.h>

#include <util/generic/algorithm.h>

#include <atomic>

namespace NYT::NObjectServer {

using namespace NHydra;
using namespace NRpc;
using namespace NBus;
using namespace NYTree;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NCypressServer;
using namespace NSecurityClient;
using namespace NSecurityServer;
using namespace NTransactionClient;
using namespace NTransactionServer;
using namespace NObjectServer;
using namespace NObjectClient;
using namespace NHiveServer;
using namespace NCellMaster;
using namespace NProfiling;
using namespace NTracing;

////////////////////////////////////////////////////////////////////////////////

class TStickyUserErrorCache
{
public:
    explicit TStickyUserErrorCache(TDuration expireTime)
        : ExpireTime_(DurationToCpuDuration(expireTime))
    { }

    TError Get(const std::string& userName)
    {
        auto now = GetCpuInstant();
        {
            auto guard = ReaderGuard(Lock_);
            auto it = Map_.find(userName);
            if (it == Map_.end()) {
                return {};
            }
            if (now < it->second.second) {
                return it->second.first;
            }
        }
        TError expiredError;
        {
            auto guard = WriterGuard(Lock_);
            auto it = Map_.find(userName);
            if (it != Map_.end() && now > it->second.second) {
                // Prevent destructing the error under spin lock.
                expiredError = std::move(it->second.first);
                Map_.erase(it);
            }
        }
        return {};
    }

    void Put(const std::string& userName, const TError& error)
    {
        auto now = GetCpuInstant();
        {
            auto guard = WriterGuard(Lock_);
            Map_.emplace(userName, std::pair(error, now + ExpireTime_));
        }
    }

private:
    const TCpuDuration ExpireTime_;

    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, Lock_);
    //! Maps user name to (error, deadline) pairs.
    THashMap<std::string, std::pair<TError, TCpuInstant>> Map_;
};

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TObjectService)

class TObjectService
    : public IObjectService
    , public NCellMaster::TMasterHydraServiceBase
{
public:
    TObjectService(
        TObjectServiceConfigPtr config,
        TBootstrap* bootstrap)
        : TMasterHydraServiceBase(
            bootstrap,
            NObjectClient::TObjectServiceProxy::GetDescriptor(),
            // Execute method is being handled in RPC thread pool anyway.
            EAutomatonThreadQueue::ObjectService,
            ObjectServerLogger(),
            TServiceOptions{
                .UseHotProfiler = false,
            })
        , Config_(std::move(config))
        , AutomatonInvoker_(Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(EAutomatonThreadQueue::ObjectService))
        , Cache_(New<TObjectServiceCache>(
            Config_->MasterCache,
            GetNullMemoryUsageTracker(),
            ObjectServerLogger(),
            ObjectServerProfiler().WithPrefix("/object_service_cache")))
        , ProcessSessionsExecutor_(New<TPeriodicExecutor>(
            AutomatonInvoker_,
            BIND(&TObjectService::ProcessSessions, MakeWeak(this))))
        , LocalReadScheduler_(CreateFairScheduler<TClosure>())
        , AutomatonScheduler_(CreateFairScheduler<TExecuteSessionPtr>())
        , LocalWriteRequestThrottler_(CreateReconfigurableThroughputThrottler(New<TThroughputThrottlerConfig>()))
        , LocalReadCallbackProvider_(New<TLocalReadCallbackProvider>(LocalReadScheduler_))
        , LocalReadExecutor_(CreateQuantizedExecutor(
            "LocalRead",
            LocalReadCallbackProvider_,
            TQuantizedExecutorOptions{
                .ThreadInitializer = MakeLocalReadThreadInitializer(),
            }))
        , LocalReadOffloadPool_(CreateThreadPool(
            /*threadCount*/ 1,
            "LocalReadOff",
            TThreadPoolOptions{
                .ThreadInitializer = MakeLocalReadThreadInitializer(),
            }))
        , StickyUserErrorCache_(Config_->StickyUserErrorExpireTime)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(Execute)
            .SetQueueSizeLimit(100'000)
            .SetConcurrencyLimit(100'000)
            .SetRequestQueueProvider(ExecuteRequestQueueProvider_)
            .SetCancelable(true)
            .SetInvoker(GetRpcInvoker())
            // NB: Execute request is always replied in heavy RPC invoker, so it should not be
            // marked as heavy.
            // Execute request handler needs request to remain alive after Reply call.
            .SetPooled(false));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GCCollect)
            .SetHeavy(true));

        DeclareServerFeature(EMasterFeature::OverlayedJournals);
        DeclareServerFeature(EMasterFeature::Portals);
        DeclareServerFeature(EMasterFeature::PortalExitSynchronization);

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        securityManager->SubscribeUserCharged(BIND_NO_PROPAGATE(&TObjectService::OnUserCharged, MakeStrong(this)));

        const auto& configManager = Bootstrap_->GetConfigManager();
        configManager->SubscribeConfigChanged(BIND_NO_PROPAGATE(&TObjectService::OnDynamicConfigChanged, MakeWeak(this)));

        ProcessSessionsExecutor_->Start();
    }

    TObjectServiceCachePtr GetCache() override
    {
        return Cache_;
    }

    IInvokerPtr CreateLocalReadInvoker(const std::string& user) override
    {
        return New<TLocalReadInvoker>(LocalReadScheduler_, user);
    }

    IInvokerPtr GetLocalReadOffloadInvoker() override
    {
        return LocalReadOffloadPool_->GetInvoker();
    }

private:
    const TObjectServiceConfigPtr Config_;

    const IInvokerPtr AutomatonInvoker_;
    const TObjectServiceCachePtr Cache_;
    const TPeriodicExecutorPtr ProcessSessionsExecutor_;

    class TExecuteSession;
    using TExecuteSessionPtr = TIntrusivePtr<TExecuteSession>;

    const IRequestQueueProviderPtr ExecuteRequestQueueProvider_ = New<TPerUserRequestQueueProvider>();

    class TSessionScheduler;

    struct TExecuteSessionInfo
    {
        TCancelableContextPtr EpochCancelableContext;
        TEphemeralObjectPtr<TUser> User;
        bool RequestQueueSizeIncreased;
    };

    //! Scheduler of callbacks to process in parallel read executor.
    const IFairSchedulerPtr<TClosure> LocalReadScheduler_;

    //! Scheduler of sessions to process in automaton.
    const IFairSchedulerPtr<TExecuteSessionPtr> AutomatonScheduler_;
    const IReconfigurableThroughputThrottlerPtr LocalWriteRequestThrottler_;

    class TLocalReadCallbackProvider
        : public ICallbackProvider
    {
    public:
        explicit TLocalReadCallbackProvider(IFairSchedulerPtr<TClosure> scheduler);

        TCallback<void()> ExtractCallback() final;

    private:
        const IFairSchedulerPtr<TClosure> Scheduler_;
    };

    const ICallbackProviderPtr LocalReadCallbackProvider_;

    class TLocalReadInvoker
        : public IInvoker
    {
    public:
        TLocalReadInvoker(
            IFairSchedulerPtr<TClosure> sessionScheduler,
            const std::string& user)
            : SessionScheduler_(std::move(sessionScheduler))
            , User_(user)
        { }

        void Invoke(TClosure callback) final
        {
            SessionScheduler_->Enqueue(std::move(callback), User_);
        }

        void Invoke(TMutableRange<TClosure> callbacks) final
        {
            for (auto&& callback : callbacks) {
                Invoke(std::move(callback));
            }
        }

        NThreading::TThreadId GetThreadId() const final
        {
            return NThreading::InvalidThreadId;
        }

        bool CheckAffinity(const IInvokerPtr& /*invoker*/) const final
        {
            return true;
        }

        bool IsSerialized() const final
        {
            return false;
        }

        void SubscribeWaitTimeObserved(const TWaitTimeObserver& /*callback*/) final
        { }

        void UnsubscribeWaitTimeObserved(const TWaitTimeObserver& /*callback*/) final
        { }

    private:
        const IFairSchedulerPtr<TClosure> SessionScheduler_;
        const std::string User_;
    };

    const IQuantizedExecutorPtr LocalReadExecutor_;
    const IThreadPoolPtr LocalReadOffloadPool_;

    TMpscStack<TExecuteSessionPtr> AutomatonReadySessions_;
    TMpscStack<TExecuteSessionPtr> LocalReadReadySessions_;
    TMpscStack<TExecuteSessionInfo> FinishedSessionInfos_;

    TStickyUserErrorCache StickyUserErrorCache_;
    std::atomic<bool> EnableTwoLevelCache_ = false;
    std::atomic<bool> EnableCypressTransactionsInSequoia_ = false;
    static constexpr auto DefaultScheduleReplyRetryBackoff = TDuration::MilliSeconds(100);
    std::atomic<TDuration> ScheduleReplyRetryBackoff_ = DefaultScheduleReplyRetryBackoff;
    std::atomic<bool> MinimizeExecuteLatency_ = false;
    static constexpr double NullPrematureBackoffAlarmProbability = -1.0;
    std::atomic<double> PrematureBackoffAlarmProbability_ = NullPrematureBackoffAlarmProbability;

    static IInvokerPtr GetRpcInvoker()
    {
        return NRpc::TDispatcher::Get()->GetHeavyInvoker();
    }

    const TDynamicObjectServiceConfigPtr& GetDynamicConfig();
    void OnDynamicConfigChanged(TDynamicClusterConfigPtr oldConfig);

    template <EUserWorkloadType WorkloadType>
    void EnqueueReadySession(TExecuteSessionPtr session);
    void EnqueueFinishedSession(TExecuteSessionInfo sessionInfo);

    void ProcessSessions();
    void FinishSession(const TExecuteSessionInfo& sessionInfo);

    void OnUserCharged(TUser* user, const TUserWorkload& workload);

    void SetStickyUserError(const std::string& userName, const TError& error);

    std::optional<double> GetPrematureBackoffAlarmProbability() const
    {
        auto prematureBackoffAlarmProbability = PrematureBackoffAlarmProbability_.load(std::memory_order::relaxed);
        // This also protects from stray NaNs.
        return prematureBackoffAlarmProbability >= 0.0
            ? std::optional(prematureBackoffAlarmProbability)
            : std::nullopt;
    }

    std::function<void()> MakeLocalReadThreadInitializer()
    {
        return [bootstrap = Bootstrap_, epochContext = Bootstrap_->GetHydraFacade()->GetEpochContext()] {
            NObjectServer::InitializeMasterStateThread(
                bootstrap,
                epochContext,
                /*isAutomatonThread*/ false);
        };
    }

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

    DECLARE_RPC_SERVICE_METHOD(NObjectClient::NProto, Execute);
    DECLARE_RPC_SERVICE_METHOD(NObjectClient::NProto, GCCollect);
};

DEFINE_REFCOUNTED_TYPE(TObjectService)

IObjectServicePtr CreateObjectService(
    TObjectServiceConfigPtr config,
    TBootstrap* bootstrap)
{
    return New<TObjectService>(
        std::move(config),
        bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EExecutionSessionSubrequestType,
    (Undefined)
    (LocalRead)
    (LocalWrite)
    (Remote)
    (Cache)
    (Sequoia)
);

DEFINE_ENUM(ESyncPhase,
    (One)
    (Two)
    (Three)
);

class TObjectService::TExecuteSession
    : public TRefCounted
{
public:
    TExecuteSession(
        TObjectServicePtr owner,
        TCtxExecutePtr rpcContext)
        : Owner_(std::move(owner))
        , RpcContext_(std::move(rpcContext))
        , TraceContext_(TryGetCurrentTraceContext())
        , Bootstrap_(Owner_->Bootstrap_)
        , TotalSubrequestCount_(RpcContext_->Request().part_counts_size())
        , Identity_(RpcContext_->GetAuthenticationIdentity())
        , RequestId_(RpcContext_->GetRequestId())
        , Codicil_(Format("RequestId: %v, %v",
            RequestId_,
            Identity_))
        , Logger(ObjectServerLogger().WithTag("RequestId: %v", RequestId_))
        , TentativePeerState_(Bootstrap_->GetHydraFacade()->GetHydraManager()->GetAutomatonState())
        , CellSyncSession_(New<TMultiPhaseCellSyncSession>(Bootstrap_, Logger))
        , PrematureBackoffAlarmProbability_(Owner_->GetPrematureBackoffAlarmProbability())
        , LocalReadInvoker_(Owner_->CreateLocalReadInvoker(Identity_.User))
        , ReplyLockCount_(TotalSubrequestCount_)
    { }

    ~TExecuteSession()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        CancelPendingCacheSubrequests();

        Owner_->EnqueueFinishedSession(TExecuteSessionInfo{
            std::move(EpochCancelableContext_),
            std::move(User_),
            RequestQueueSizeIncremented_,
        });
    }

    const std::string& GetUserName() const
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        return Identity_.User;
    }

    void RunRpc()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        try {
            GuardedRunRpc();
        } catch (const std::exception& ex) {
            Reply(ex);
        }
    }

    template <EUserWorkloadType WorkloadType>
    void OnDequeued()
    {
        Enqueued<WorkloadType>().store(false);
    }

    template <EUserWorkloadType WorkloadType>
    std::atomic<bool>& Enqueued()
    {
        if constexpr (WorkloadType == EUserWorkloadType::Write) {
            return EnqueuedToAutomaton_;
        } else if constexpr (WorkloadType == EUserWorkloadType::Read) {
            return EnqueuedToLocalRead_;
        } else {
            static_assert("Unexpected workload type for enqueuing/dequeuing execute session");
        }
    }

    bool RunAutomatonFast(EUserWorkloadType workloadType)
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        try {
            return GuardedRunAutomatonFast(workloadType);
        } catch (const std::exception& ex) {
            Reply(ex);
            return false;
        }
    }

    TRequestId GetRequestId() const
    {
        return RequestId_;
    }

    void RunAutomatonSlow()
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        auto codicilGuard = MakeCodicilGuard();
        try {
            GuardedRunAutomatonSlow();
        } catch (const std::exception& ex) {
            Reply(ex);
        }
    }

    void RunRead()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto codicilGuard = MakeCodicilGuard();
        try {
            GuardedRunRead();
        } catch (const std::exception& ex) {
            Reply(ex);
        }
    }

    TTraceContextPtr GetTraceContext() const
    {
        return TraceContext_;
    }

private:
    const TObjectServicePtr Owner_;
    const TCtxExecutePtr RpcContext_;
    const TTraceContextPtr TraceContext_;

    NCellMaster::TBootstrap* const Bootstrap_;
    const int TotalSubrequestCount_;
    const NRpc::TAuthenticationIdentity& Identity_;
    const TRequestId RequestId_;
    const std::string Codicil_;
    const NLogging::TLogger Logger;
    const EPeerState TentativePeerState_;
    const TMultiPhaseCellSyncSessionPtr CellSyncSession_;
    const std::optional<double> PrematureBackoffAlarmProbability_;

    TDelayedExecutorCookie BackoffAlarmCookie_;

    struct TSubrequest
    {
        int Index = -1;
        bool TentativelyRemote = false;
        EExecutionSessionSubrequestType Type = EExecutionSessionSubrequestType::Undefined;
        IYPathServiceContextPtr RpcContext;
        std::unique_ptr<TMutation> Mutation;
        std::atomic<bool> ActiveCacheCookieSet = false;
        TObjectServiceCache::TCookie ActiveCacheCookie;
        NRpc::NProto::TRequestHeader RequestHeader;
        const NYTree::NProto::TYPathHeaderExt* YPathExt = nullptr;
        const NObjectClient::NProto::TPrerequisitesExt* PrerequisitesExt = nullptr;
        const NObjectClient::NProto::TMulticellSyncExt* MulticellSyncExt = nullptr;
        TSharedRefArray RequestMessage;
        TCellTag ForwardedCellTag = InvalidCellTag;
        std::optional<TYPathRewrite> TargetPathRewrite;
        std::optional<TCompactVector<TYPathRewrite, TypicalAdditionalPathCount>> AdditionalPathRewrites;
        std::optional<TCompactVector<TYPathRewrite, 4>> PrerequisiteRevisionPathRewrites;
        TSharedRefArray RemoteRequestMessage;
        TSharedRefArray ResponseMessage;
        TTraceContextPtr TraceContext;
        NHydra::TRevision Revision = NHydra::NullRevision;
        std::atomic<bool> Uncertain = false;
        std::atomic<bool> Started = false;
        std::atomic<bool> Completed = false;
        TRequestProfilingCountersPtr ProfilingCounters;
        // Only for (local) write requests. (Local read requests are handled by
        // a session-wide replication session).
        TTransactionReplicationSessionWithBoomerangsPtr RemoteTransactionReplicationSession;
        // For local reads, this is a future that will be set when all remote
        // transactions have actually been replicated here.
        // Mutually exclusive with MutationResponseFuture.
        TFuture<void> RemoteTransactionReplicationFuture;
        // For local writes, this is a future that is set when the mutation is applied. That mutation
        // may be either committed in the ordinary fashion or posted as a boomerang.
        // Mutually exclusive with RemoteTransactionReplicationFuture.
        TFuture<TMutationResponse> MutationResponseFuture;

        TReadRequestComplexityOverrides ReadRequestComplexityOverrides;
    };

    // For (local) read requests. (Write requests are handled by per-subrequest replication sessions.)
    TTransactionReplicationSessionWithoutBoomerangsPtr RemoteTransactionReplicationSession_;

    // Some subrequests can be outsourced (either by forwarding to remote
    // service or by subscribing to cache). A session should not be destroyed
    // until such subrequests are processed, but neither should it be kept alive
    // for too long because backoff alarms, timeouts and the like may require
    // early destruction. Blithely capturing a strong reference per outsourced
    // subrequest may lead to a host of ugly problems, cyclic references between
    // multiple sessions through cache subscriptions being one of them.
    //
    // Basically, there're two ways of dealing with session lifetime here:
    //
    //   1. Capture a strong reference per outsourced subrequest but be prepared
    //     to cancel those subscriptions and destroy corresponding references.
    //
    //   2. Capture a weak reference per outsourced subrequest and manage the
    //     lifetime centrally. #SelfReference_ below does exactly that.
    //
    // The first way leaves less garbage around (it destroys not only the
    // session but subscription callbacks). However, it's hard to implement
    // efficiently and race-free because, firstly, a vector of cancellation
    // cookies would require careful synchronization and, secondly, cancelling a
    // subscription is inherently racy and would require even more
    // synchronization. Oh, and don't forget exceptions that may occur while the
    // vector is half-filled.
    //
    // With the second way, a lot of (no-op) callbacks may outlive a session
    // object (for a time). But otherwise it's simpler.
    TIntrusivePtr<TExecuteSession> SelfReference_;

    THashMap<TTransactionId, TCompactVector<TSubrequest*, 1>> RemoteTransactionIdToSubrequests_;

    std::unique_ptr<TSubrequest[]> Subrequests_;

    // Indices of subrequests to be executed.
    int CurrentAutomatonSubrequestIndex_ = 0;
    int CurrentLocalReadSubrequestIndex_ = 0;

    // Indices of last throttled subrequests.
    int ThrottledAutomatonSubrequestIndex_ = -1;
    int ThrottledLocalReadSubrequestIndex_ = -1;

    TCancelableContextPtr EpochCancelableContext_;

    TEphemeralObjectPtr<TUser> User_;
    // NB: LocalRead invoker is user-specific and, consequently, session-specific.
    IInvokerPtr LocalReadInvoker_;

    struct TReadRequestComplexityLimits
    {
        TReadRequestComplexity Default;
        TReadRequestComplexity Max;
    };
    std::optional<TReadRequestComplexityLimits> ReadRequestComplexityLimits_;

    bool SuppressTransactionCoordinatorSync_ = false;
    bool NeedsUserAccessValidation_ = true;
    bool RequestQueueSizeIncremented_ = false;

    std::atomic<bool> ReplyScheduled_ = false;
    std::atomic<bool> LocalExecutionInterrupted_ = false;

    // Has the time to backoff come?
    std::atomic<bool> BackoffAlarmTriggered_ = false;

    // Once this drops to zero, the request can be replied.
    // Initialized with the total number of subrequests (i.e. batch size).
    std::atomic<int> ReplyLockCount_;

    // Set to true if we're ready to reply with at least one subresponse.
    std::atomic<bool> SomeSubrequestCompleted_ = false;

    std::atomic<bool> EnqueuedToAutomaton_ = false;
    std::atomic<bool> EnqueuedToLocalRead_ = false;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);


    void GuardedRunRpc()
    {
        auto codicilGuard = MakeCodicilGuard();

        const auto& request = RpcContext_->Request();

        auto originalRequestId = FromProto<TRequestId>(request.original_request_id());

        RpcContext_->SetRequestInfo("SubrequestCount: %v, SuppressUpstreamSync: %v, "
            "SuppressTransactionCoordinatorSync: %v, OriginalRequestId: %v, AllowResolveFromSequoiaObject: %v",
            TotalSubrequestCount_,
            GetSuppressUpstreamSync(RpcContext_),
            GetSuppressTransactionCoordinatorSync(RpcContext_),
            originalRequestId,
            GetAllowResolveFromSequoiaObject(RpcContext_->GetRequestHeader()));

        if (TotalSubrequestCount_ == 0) {
            Reply();
            return;
        }

        if (TentativePeerState_ != EPeerState::Leading && TentativePeerState_ != EPeerState::Following) {
            THROW_ERROR_EXCEPTION(NRpc::EErrorCode::Unavailable, "Hydra peer is not active");
        }

        ScheduleBackoffAlarm();
        ParseSubrequests();
        MarkTentativelyRemoteSubrequests();
        // Necessary to determine which tx coordinator cells require explicitly
        // syncing with, and which will be handled implicitly by tx replication.
        CheckSubrequestsForRemoteTransactions();
        RunSyncPhaseOne();
    }

    void ParseSubrequests()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        const auto& request = RpcContext_->Request();
        const auto& attachments = RpcContext_->RequestAttachments();

        Subrequests_.reset(new TSubrequest[TotalSubrequestCount_]);

        auto now = NProfiling::GetInstant();

        auto suppressUpstreamSync = GetSuppressUpstreamSync(RpcContext_);
        auto suppressTransactionCoordinatorSync = GetSuppressTransactionCoordinatorSync(RpcContext_);
        int currentPartIndex = 0;
        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            subrequest.Index = subrequestIndex;

            int partCount = request.part_counts(subrequestIndex);
            TSharedRefArrayBuilder subrequestPartsBuilder(partCount);
            for (int partIndex = 0; partIndex < partCount; ++partIndex) {
                subrequestPartsBuilder.Add(attachments[currentPartIndex++]);
            }
            subrequest.RequestMessage = subrequestPartsBuilder.Finish();

            auto& requestHeader = subrequest.RequestHeader;
            if (!TryParseRequestHeader(subrequest.RequestMessage, &requestHeader)) {
                THROW_ERROR_EXCEPTION(
                    NRpc::EErrorCode::ProtocolError,
                    "Error parsing subrequest header");
            }

            const auto& requestProfilingManager = Bootstrap_->GetRequestProfilingManager();
            subrequest.ProfilingCounters = requestProfilingManager->GetCounters(
                RpcContext_->GetAuthenticationIdentity().UserTag,
                requestHeader.method());

            // Propagate various parameters to the subrequest.
            if (!requestHeader.has_request_id()) {
                ToProto(requestHeader.mutable_request_id(), RequestId_);
            }
            if (RpcContext_->IsRetry()) {
                requestHeader.set_retry(true);
            }
            if (!requestHeader.has_user()) {
                NRpc::WriteAuthenticationIdentityToProto(&requestHeader, RpcContext_->GetAuthenticationIdentity());
            }
            if (!requestHeader.has_timeout()) {
                requestHeader.set_timeout(ToProto(RpcContext_->GetTimeout().value_or(Owner_->Config_->DefaultExecuteTimeout)));
            }
            if (!requestHeader.has_start_time()) {
                requestHeader.set_start_time(ToProto(RpcContext_->GetStartTime().value_or(now)));
            }
            if (GetSuppressUpstreamSync(RpcContext_)) {
                SetSuppressUpstreamSync(&requestHeader, true);
            }
            if (GetSuppressTransactionCoordinatorSync(RpcContext_)) {
                SetSuppressTransactionCoordinatorSync(&requestHeader, true);
            }

            auto* ypathExt = requestHeader.MutableExtension(NYTree::NProto::TYPathHeaderExt::ypath_header_ext);
            subrequest.YPathExt = ypathExt;

            subrequest.PrerequisitesExt = &requestHeader.GetExtension(NObjectClient::NProto::TPrerequisitesExt::prerequisites_ext);

            subrequest.MulticellSyncExt = &requestHeader.GetExtension(NObjectClient::NProto::TMulticellSyncExt::multicell_sync_ext);
            suppressUpstreamSync = suppressUpstreamSync ||
                subrequest.MulticellSyncExt->suppress_upstream_sync();
            suppressTransactionCoordinatorSync = suppressTransactionCoordinatorSync ||
                subrequest.MulticellSyncExt->suppress_transaction_coordinator_sync();

            // Store original path.
            if (!ypathExt->has_original_target_path()) {
                ypathExt->set_original_target_path(ypathExt->target_path());
            }

            if (ypathExt->original_additional_paths_size() == 0) {
                *ypathExt->mutable_original_additional_paths() = ypathExt->additional_paths();
            }

            if (subrequest.YPathExt->mutating()) {
                if (!FromProto<TMutationId>(requestHeader.mutation_id())) {
                    ToProto(requestHeader.mutable_mutation_id(), NRpc::GenerateMutationId());
                }
            }

            subrequest.RequestMessage = SetRequestHeader(subrequest.RequestMessage, requestHeader);

            if (ypathExt->mutating()) {
                subrequest.ProfilingCounters->TotalWriteRequestCounter.Increment();
            } else {
                subrequest.ProfilingCounters->TotalReadRequestCounter.Increment();
            }

            if (ypathExt->has_read_complexity_limits()) {
                FromProto(&subrequest.ReadRequestComplexityOverrides, ypathExt->read_complexity_limits());
            }
        }

        CellSyncSession_->SetSyncWithUpstream(!suppressUpstreamSync);
        SuppressTransactionCoordinatorSync_ = suppressTransactionCoordinatorSync;
    }

    void LookupCachedSubrequests()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!Owner_->EnableTwoLevelCache_) {
            return;
        }

        TCompactVector<std::pair<int, TFuture<TObjectServiceCacheEntryPtr>>, 4> pendingCacheSubscriptions;
        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            const auto& requestHeader = subrequest.RequestHeader;
            if (!requestHeader.HasExtension(NYTree::NProto::TCachingHeaderExt::caching_header_ext)) {
                continue;
            }

            if (subrequest.YPathExt->mutating()) {
                THROW_ERROR_EXCEPTION(
                    NObjectClient::EErrorCode::CannotCacheMutatingRequest,
                    "Mutating requests cannot be cached");
            }

            const auto& cachingRequestHeaderExt = requestHeader.GetExtension(NYTree::NProto::TCachingHeaderExt::caching_header_ext);
            const auto multicellSyncExt = requestHeader.GetExtension(NObjectClient::NProto::TMulticellSyncExt::multicell_sync_ext);
            TObjectServiceCacheKey key(
                Bootstrap_->GetCellTag(),
                cachingRequestHeaderExt.disable_per_user_cache() ? std::string() : RpcContext_->GetAuthenticationIdentity().User,
                subrequest.YPathExt->target_path(),
                requestHeader.service(),
                requestHeader.method(),
                subrequest.RequestMessage[1],
                multicellSyncExt.suppress_upstream_sync(),
                multicellSyncExt.suppress_transaction_coordinator_sync());

            auto refreshRevision = cachingRequestHeaderExt.refresh_revision();
            auto cookie = Owner_->Cache_->BeginLookup(
                RequestId_,
                key,
                FromProto<TDuration>(cachingRequestHeaderExt.expire_after_successful_update_time()),
                FromProto<TDuration>(cachingRequestHeaderExt.expire_after_failed_update_time()),
                FromProto<TDuration>(cachingRequestHeaderExt.success_staleness_bound()),
                FromProto<NHydra::TRevision>(refreshRevision));

            YT_LOG_DEBUG("Serving subrequest from cache (SubrequestIndex: %v, Key: %v, CookieActive: %v)",
                subrequestIndex,
                key,
                cookie.IsActive());

            if (cookie.IsActive()) {
                subrequest.ActiveCacheCookie = std::move(cookie);
                subrequest.ActiveCacheCookieSet.store(true);
            } else {
                // NB: Postpone subscribing to the cookie to avoid races with CancelPendingCacheSubrequests.
                pendingCacheSubscriptions.emplace_back(subrequestIndex, cookie.GetValue());
                subrequest.Type = EExecutionSessionSubrequestType::Cache;
                // Strictly speaking, this is unnecessary. The Started flag is
                // only used to mark subrequests as uncertain (when the session
                // is interrupted mid-execution). And uncertainty is only used to
                // properly set retry flags, which aren't required for read
                // subrequests. So this is just for uniformity's sake.
                subrequest.Started.store(true);
            }
        }

        for (const auto& [subrequestIndex, future] : pendingCacheSubscriptions) {
            // MakeWeak is sufficient - see SelfReference_.
            future.Subscribe(BIND([weakThis = MakeWeak(this), subrequestIndex] (const TErrorOr<TObjectServiceCacheEntryPtr>& entryOrError) {
                auto this_ = weakThis.Lock();
                if (!this_) {
                    return;
                }

                auto& subrequest = this_->Subrequests_[subrequestIndex];
                if (!entryOrError.IsOK()) {
                    if (entryOrError.FindMatching(NYT::EErrorCode::Canceled)) {
                        this_->Reply(TError(NRpc::EErrorCode::TransientFailure, "Transient failure")
                            << entryOrError);
                    } else {
                        this_->Reply(entryOrError);
                    }
                    return;
                }
                const auto& entry = entryOrError.Value();
                subrequest.Revision = entry->GetRevision();
                this_->OnCompletedSubresponse(&subrequest, entry->GetResponseMessage());
            }));
        }
    }

    TCellTagList CollectCellsToSyncForLocalExecution(ESyncPhase syncPhase)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        TCellTagList result;
        auto addCellTagToSyncWith = [&] (TCellTag cellTag) {
            if (std::find(result.begin(), result.end(), cellTag) == result.end()) {
                result.push_back(cellTag);
            }
        };
        auto addCellTagsToSyncWith = [&] (const TCellTagList& cellTags) {
            for (auto cellTag : cellTags) {
                addCellTagToSyncWith(cellTag);
            }
        };

        if (!SuppressTransactionCoordinatorSync_) {
            switch (syncPhase) {
                case ESyncPhase::One:
                case ESyncPhase::Two:
                    addCellTagsToSyncWith(
                        RemoteTransactionReplicationSession_->GetCellTagsToSyncWithDuringInvocation());
                    break;

                case ESyncPhase::Three:
                    addCellTagsToSyncWith(
                        RemoteTransactionReplicationSession_->GetCellTagsToSyncWithAfterInvocation());
                    break;

                default:
                    YT_ABORT();
            }
        }

        const auto& multicellExt = RpcContext_->RequestHeader().GetExtension(NObjectClient::NProto::TMulticellSyncExt::multicell_sync_ext);
        for (auto protoCellTag : multicellExt.cell_tags_to_sync_with()) {
            auto cellTag = FromProto<TCellTag>(protoCellTag);
            if (cellTag == Bootstrap_->GetCellTag()) {
                continue;
            }
            addCellTagToSyncWith(cellTag);
        }

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            const auto& subrequest = Subrequests_[subrequestIndex];
            if (subrequest.Type == EExecutionSessionSubrequestType::Undefined && subrequest.TentativelyRemote) {
                // Phase one.
                continue;
            }
            if (subrequest.Type == EExecutionSessionSubrequestType::Remote ||
                subrequest.Type == EExecutionSessionSubrequestType::Cache)
            {
                // Phase two.
                continue;
            }

            if (!SuppressTransactionCoordinatorSync_ && subrequest.RemoteTransactionReplicationSession) {
                addCellTagsToSyncWith(
                    subrequest.RemoteTransactionReplicationSession->GetCellTagsToSyncWithBeforeInvocation());
            }

            for (auto protoCellTag : subrequest.MulticellSyncExt->cell_tags_to_sync_with()) {
                auto cellTag = FromProto<TCellTag>(protoCellTag);
                if (cellTag == Bootstrap_->GetCellTag()) {
                    continue;
                }
                addCellTagToSyncWith(cellTag);
            }
        }

        SortUnique(result);
        return result;
    }

    bool IsTentativelyRemoteSubrequest(const TSubrequest& subrequest)
    {
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        const auto& resolveCache = cypressManager->GetResolveCache();
        auto resolveResult = resolveCache->TryResolve(subrequest.YPathExt->target_path());
        if (resolveResult) {
            return true;
        }

        if (subrequest.YPathExt->mutating() && TentativePeerState_ != EPeerState::Leading) {
            return true;
        }

        return false;
    }

    void MarkTentativelyRemoteSubrequests()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            subrequest.TentativelyRemote = IsTentativelyRemoteSubrequest(subrequest);
        }
    }

    TFuture<void> StartSync(ESyncPhase syncPhase, TFuture<void> additionalFuture = {})
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto cellTags = CollectCellsToSyncForLocalExecution(syncPhase);

        const auto& hydraFacade = Bootstrap_->GetHydraFacade();
        const auto& hydraManager = hydraFacade->GetHydraManager();
        if (!cellTags.empty() && hydraManager->GetReadOnly()) {
            THROW_ERROR_EXCEPTION(
                NHydra::EErrorCode::ReadOnly,
                "Cannot synchronize with cells when read-only mode is active");
        }

        // NB: We always have to wait all current prepared transactions to
        // observe side effects of Sequoia transactions.
        const auto& transactionSupervisor = Bootstrap_->GetTransactionSupervisor();
        std::vector<TFuture<void>> additionalFutures = {
            transactionSupervisor->WaitUntilPreparedTransactionsFinished(),
        };
        if (additionalFuture) {
            additionalFutures.push_back(std::move(additionalFuture));
        }

        return CellSyncSession_->Sync(cellTags, std::move(additionalFutures));
    }

    void RunSyncPhaseOne()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto future = StartSync(ESyncPhase::One);
        if (future.IsSet()) {
            OnSyncPhaseOneCompleted(future.Get());
        } else {
            future.Subscribe(
                BIND(&TExecuteSession::OnSyncPhaseOneCompleted, MakeStrong(this))
                    .Via(GetRpcInvoker()));
        }
    }

    void OnSyncPhaseOneCompleted(const TError& error = {})
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!error.IsOK()) {
            Reply(error);
            return;
        }

        try {
            LookupCachedSubrequests();

            SelfReference_ = MakeStrong(this);

            // This shouldn't be done any sooner to avoid races between
            // CancelPendingCacheSubrequests and LookupCachedSubrequests.
            SubscribeToCancelation();

            // Re-check remote requests to see if resolve cache resolve is still OK.
            DecideSubrequestTypes();

            ValidateRequestsFeatures();

            ForwardRemoteRequests();

            // Re-check so that
            //   - previously assumed to be remote but actually local subrequests are handled correctly, and
            //   - previously assumed to be local but actually remote subrequests are ignored.
            CheckSubrequestsForRemoteTransactions();

            RunSyncPhaseTwo();
        } catch (const std::exception& ex) {
            Reply(ex);
        }
    }

    void SubscribeToCancelation()
    {
        RpcContext_->SubscribeCanceled(BIND(&TExecuteSession::OnCanceled, MakeWeak(this)));
    }

    void OnCanceled(const TError& error)
    {
        // Since RpcContext_->IsCanceled() is already true, this will not actually
        // send a reply (see DoReply). Nevertheless, this is crucial for releasing
        // #SelfReference_ in case of a timeout.
        Reply(error);
    }

    void MarkSubrequestLocal(TSubrequest* subrequest)
    {
        auto mutating = subrequest->YPathExt->mutating();

        if (mutating && TentativePeerState_ != EPeerState::Leading) {
            Reply(TError(NRpc::EErrorCode::Unavailable, "Peer is not leading"));
            return;
        }

        subrequest->RpcContext = CreateYPathContext(
            subrequest->RequestMessage,
            ObjectServerLogger(),
            NLogging::ELogLevel::Debug);

        if (mutating) {
            const auto& objectManager = Bootstrap_->GetObjectManager();
            subrequest->Mutation = objectManager->CreateExecuteMutation(subrequest->RpcContext, subrequest->RpcContext->GetAuthenticationIdentity());
            subrequest->Mutation->SetMutationId(subrequest->RpcContext->GetMutationId(), subrequest->RpcContext->IsRetry());
            subrequest->Mutation->SetTraceContext(TraceContext_);
            subrequest->Type = EExecutionSessionSubrequestType::LocalWrite;
            subrequest->ProfilingCounters->LocalWriteRequestCounter.Increment();
        } else {
            subrequest->Type = EExecutionSessionSubrequestType::LocalRead;
            subrequest->ProfilingCounters->LocalReadRequestCounter.Increment();
        }
    }

    void MarkSubrequestRemoteCrossCell(TSubrequest* subrequest, TCellTag forwardedCellTag)
    {
        auto remoteRequestHeader = subrequest->RequestHeader;
        auto* remoteYPathExt = remoteRequestHeader.MutableExtension(NYTree::NProto::TYPathHeaderExt::ypath_header_ext);
        auto* remotePrerequisitesExt = remoteRequestHeader.MutableExtension(NObjectClient::NProto::TPrerequisitesExt::prerequisites_ext);

        if (subrequest->TargetPathRewrite) {
            remoteYPathExt->set_target_path(subrequest->TargetPathRewrite->Rewritten);
        }

        if (subrequest->AdditionalPathRewrites) {
            remoteYPathExt->clear_additional_paths();
            for (const auto& rewrite : *subrequest->AdditionalPathRewrites) {
                remoteYPathExt->add_additional_paths(rewrite.Rewritten);
            }
        }

        if (subrequest->PrerequisiteRevisionPathRewrites) {
            YT_VERIFY(static_cast<int>(subrequest->PrerequisiteRevisionPathRewrites->size()) == remotePrerequisitesExt->revisions_size());
            for (int index = 0; index < remotePrerequisitesExt->revisions_size(); ++index) {
                remotePrerequisitesExt->mutable_revisions(index)->set_path((*subrequest->PrerequisiteRevisionPathRewrites)[index].Rewritten);
            }
        }

        if (auto mutationId = NRpc::GetMutationId(remoteRequestHeader)) {
            SetMutationId(&remoteRequestHeader, GenerateNextForwardedMutationId(mutationId), remoteRequestHeader.retry());
        }

        subrequest->ForwardedCellTag = forwardedCellTag;
        subrequest->RemoteRequestMessage = SetRequestHeader(subrequest->RequestMessage, remoteRequestHeader);
        subrequest->Type = EExecutionSessionSubrequestType::Remote;
        subrequest->ProfilingCounters->CrossCellForwardingRequestCounter.Increment();
    }

    void MarkSubrequestSequoia(
        TSubrequest* subrequest,
        const TResolveCache::TSequoiaResolveResult& resolveResult)
    {
        subrequest->Type = EExecutionSessionSubrequestType::Sequoia;
        OnCompletedSubresponse(
            subrequest,
            CreateErrorResponseMessage(TError(
                NObjectClient::EErrorCode::RequestInvolvesSequoia,
                "Request resolved in Sequoia")
                << TErrorAttribute("rootstock_node_id", resolveResult.RootstockNodeId)
                << TErrorAttribute("rootstock_path", resolveResult.RootstockPath)));
        YT_LOG_DEBUG(
            "Request redirected to Sequoia (Subrequest index: %v, TargetPath: %v, "
            "RootstockNodeId: %v, RootstockPath: %v)",
            subrequest->Index,
            subrequest->YPathExt->target_path(),
            resolveResult.RootstockNodeId,
            resolveResult.RootstockPath);
    }

    void DecideSubrequestType(TSubrequest* subrequest)
    {
        if (subrequest->Type == EExecutionSessionSubrequestType::Cache) {
            return;
        }

        const auto& cypressManager = Bootstrap_->GetCypressManager();
        const auto& resolveCache = cypressManager->GetResolveCache();

        auto targetResolveResult = resolveCache->TryResolve(subrequest->YPathExt->target_path());
        if (!targetResolveResult) {
            MarkSubrequestLocal(subrequest);
            return;
        }

        if (const auto* sequoiaResolveResult = std::get_if<TResolveCache::TSequoiaResolveResult>(&*targetResolveResult)) {
            MarkSubrequestSequoia(subrequest, *sequoiaResolveResult);
            return;
        }

        const auto& targetRemoteResolveResult = std::get<TResolveCache::TRemoteResolveResult>(*targetResolveResult);

        subrequest->TargetPathRewrite = MakeYPathRewrite(
            subrequest->YPathExt->target_path(),
            targetRemoteResolveResult.RemoteNodeId,
            targetRemoteResolveResult.UnresolvedPathSuffix);

        subrequest->AdditionalPathRewrites.emplace();
        subrequest->AdditionalPathRewrites->reserve(subrequest->YPathExt->additional_paths_size());
        for (const auto& additionalPath : subrequest->YPathExt->additional_paths()) {
            auto additionalResolveResult = resolveCache->TryResolve(additionalPath);
            if (!additionalResolveResult) {
                MarkSubrequestLocal(subrequest);
                return;
            }

            if (const auto* sequoiaResolveResult = std::get_if<TResolveCache::TSequoiaResolveResult>(&*additionalResolveResult)) {
                MarkSubrequestSequoia(subrequest, *sequoiaResolveResult);
                return;
            }

            const auto& additionalRemoteResolveResult = std::get<TResolveCache::TRemoteResolveResult>(*additionalResolveResult);

            if (CellTagFromId(additionalRemoteResolveResult.RemoteNodeId) != CellTagFromId(targetRemoteResolveResult.RemoteNodeId)) {
                MarkSubrequestLocal(subrequest);
                return;
            }

            subrequest->AdditionalPathRewrites->push_back(MakeYPathRewrite(
                additionalPath,
                additionalRemoteResolveResult.RemoteNodeId,
                additionalRemoteResolveResult.UnresolvedPathSuffix));
        }

        subrequest->PrerequisiteRevisionPathRewrites.emplace();
        for (const auto& prerequisite : subrequest->PrerequisitesExt->revisions()) {
            const auto& prerequisitePath = prerequisite.path();
            auto prerequisiteResolveResult = resolveCache->TryResolve(prerequisitePath);
            if (!prerequisiteResolveResult) {
                MarkSubrequestLocal(subrequest);
                return;
            }

            if (const auto* sequoiaResolveResult = std::get_if<TResolveCache::TSequoiaResolveResult>(&*prerequisiteResolveResult)) {
                MarkSubrequestSequoia(subrequest, *sequoiaResolveResult);
                return;
            }

            const auto& prerequisiteRemoteResolveResult = std::get<TResolveCache::TRemoteResolveResult>(*prerequisiteResolveResult);

            if (CellTagFromId(prerequisiteRemoteResolveResult.RemoteNodeId) != CellTagFromId(targetRemoteResolveResult.RemoteNodeId)) {
                MarkSubrequestLocal(subrequest);
                return;
            }

            subrequest->PrerequisiteRevisionPathRewrites->push_back(MakeYPathRewrite(
                prerequisitePath,
                prerequisiteRemoteResolveResult.RemoteNodeId,
                prerequisiteRemoteResolveResult.UnresolvedPathSuffix));
        }

        if (IsAlienType(TypeFromId(targetRemoteResolveResult.RemoteNodeId))) {
            MarkSubrequestLocal(subrequest);
            return;
        }

        MarkSubrequestRemoteCrossCell(subrequest, CellTagFromId(targetRemoteResolveResult.RemoteNodeId));
    }

    void DecideSubrequestTypes()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            DecideSubrequestType(&subrequest);
        }
    }

    void ValidateRequestsFeatures()
    {
        for (int index = 0; index < TotalSubrequestCount_; ++index) {
            const auto& subrequest = Subrequests_[index];
            if (subrequest.RpcContext) {
                Owner_->ValidateRequestFeatures(subrequest.RpcContext);
            }
        }
    }

    void CheckSubrequestsForRemoteTransactions()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto addSubrequestTransactions = [&] (
            std::vector<TTransactionId>* transactions,
            TSubrequest& subrequest,
            THashMap<TTransactionId, TCompactVector<TSubrequest*,1>>* transactionIdToSubrequests)
        {
            auto doTransaction = [&] (TTransactionId transactionId) {
                transactions->push_back(transactionId);

                if (transactionIdToSubrequests) {
                    auto& subrequests = (*transactionIdToSubrequests)[transactionId];
                    if (std::find(subrequests.begin(), subrequests.end(), &subrequest) == subrequests.end()) {
                        subrequests.push_back(&subrequest);
                    }
                }
            };

            doTransaction(GetTransactionId(subrequest.RequestHeader));

            for (const auto& prerequisite : subrequest.PrerequisitesExt->transactions()) {
                doTransaction(FromProto<TTransactionId>(prerequisite.transaction_id()));
            }
        };

        std::vector<TTransactionId> transactionsToReplicateWithoutBoomerangs;

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];

            // Phase one.
            if (subrequest.Type == EExecutionSessionSubrequestType::Undefined && subrequest.TentativelyRemote) {
                continue;
            }

            // Phase two.
            if (subrequest.Type == EExecutionSessionSubrequestType::Sequoia) {
                continue;
            }

            if (subrequest.Type == EExecutionSessionSubrequestType::Remote ||
                subrequest.Type == EExecutionSessionSubrequestType::Cache)
            {
                // Some non-tentatively-remote subrequests may have become remote.
                if (subrequest.RemoteTransactionReplicationSession) {
                    // Remove the session as it won't be used.
                    subrequest.RemoteTransactionReplicationSession.Reset();
                }

                continue;
            }

            YT_VERIFY(
                subrequest.Type == EExecutionSessionSubrequestType::Undefined ||
                subrequest.Type == EExecutionSessionSubrequestType::LocalRead ||
                subrequest.Type == EExecutionSessionSubrequestType::LocalWrite);

            if (subrequest.YPathExt->mutating()) {
                if (!subrequest.RemoteTransactionReplicationSession) {
                    // Pre-phase-one or previously-tentatively-remote-but-no-longer-remote subrequest.
                    std::vector<TTransactionId> writeSubrequestTransactions;
                    addSubrequestTransactions(&writeSubrequestTransactions, subrequest, nullptr);
                    subrequest.RemoteTransactionReplicationSession = New<TTransactionReplicationSessionWithBoomerangs>(
                        Bootstrap_,
                        std::move(writeSubrequestTransactions),
                        TTransactionReplicationInitiatorRequestInfo{
                            .Identity = Identity_,
                            .RequestId = RequestId_,
                            .SubrequestIndex = subrequestIndex,
                        },
                        Owner_->EnableCypressTransactionsInSequoia_.load(std::memory_order::acquire));
                }
                if (subrequest.Mutation) {
                    // Pre-phase-two.
                    subrequest.RemoteTransactionReplicationSession->SetMutation(std::move(subrequest.Mutation));
                }
            } else {
                addSubrequestTransactions(
                    &transactionsToReplicateWithoutBoomerangs,
                    subrequest,
                    &RemoteTransactionIdToSubrequests_);
            }
        }

        if (!RemoteTransactionReplicationSession_) {
            // Pre-phase-one.
            RemoteTransactionReplicationSession_ = New<TTransactionReplicationSessionWithoutBoomerangs>(
                Bootstrap_,
                std::move(transactionsToReplicateWithoutBoomerangs),
                TTransactionReplicationInitiatorRequestInfo{
                    .Identity = Identity_,
                    .RequestId = RequestId_,
                },
                Owner_->EnableCypressTransactionsInSequoia_.load(std::memory_order::acquire));
        } else {
            // Pre-phase-two.
            RemoteTransactionReplicationSession_->Reset(std::move(transactionsToReplicateWithoutBoomerangs));
        }
    }

    TFuture<void> InvokeRemoteTransactionReplicationWithoutBoomerangs()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto future = RemoteTransactionReplicationSession_->InvokeReplicationRequests();
        if (!future) {
            return VoidFuture;
        }

        return future.Apply(BIND(
            [this, this_ = MakeStrong(this)]
            (const THashMap<TTransactionId, TFuture<void>>& transactionReplicationFutures) {
                YT_ASSERT_THREAD_AFFINITY_ANY();

                THashMap<TSubrequest*, std::vector<TFuture<void>>> subrequestToRemoteTransactionReplicationFutures;

                for (auto& [transactionId, replicationFuture] : transactionReplicationFutures) {
                    YT_VERIFY(replicationFuture);

                    auto it = RemoteTransactionIdToSubrequests_.find(transactionId);
                    YT_VERIFY(it != RemoteTransactionIdToSubrequests_.end());
                    const auto& transactionSubrequests = it->second;
                    for (auto* subrequest : transactionSubrequests) {
                        subrequestToRemoteTransactionReplicationFutures[subrequest].push_back(replicationFuture);
                    }
                }

                for (const auto& [subrequest, replicationFutures] : subrequestToRemoteTransactionReplicationFutures) {
                    YT_VERIFY(!subrequest->RemoteTransactionReplicationFuture);
                    subrequest->RemoteTransactionReplicationFuture = AllSucceeded(std::move(replicationFutures));
                }
            }));
    }

    void RunSyncPhaseTwo()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        // Read subrequests may request their remote transaction replication
        // right away, as they will subscribe and wait for any such transaction
        // later.
        // For write subrequests, boomerangs must be launched after syncing with
        // the rest of the cells (because there's no other way to guarantee that
        // boomerangs will come back *after* the syncing is done).
        auto replicationFuture = InvokeRemoteTransactionReplicationWithoutBoomerangs();

        auto future = StartSync(ESyncPhase::Two, std::move(replicationFuture));
        if (future.IsSet()) {
            OnSyncPhaseTwoCompleted(future.Get());
        } else {
            future.Subscribe(
                BIND(&TExecuteSession::OnSyncPhaseTwoCompleted, MakeStrong(this))
                    .Via(GetRpcInvoker()));
        }
    }

    void OnSyncPhaseTwoCompleted(const TError& error = {})
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!error.IsOK()) {
            Reply(error);
            return;
        }

        try {
            RunSyncPhaseThree();
        } catch (const std::exception& ex) {
            Reply(ex);
        }
    }

    void RunSyncPhaseThree()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto future = StartSync(ESyncPhase::Three);
        if (future.IsSet()) {
            // NB: Sync-phase-three is usually no-op, so this is the common case.
            OnSyncPhaseThreeCompleted(future.Get());
        } else {
            future.Subscribe(
                BIND(&TExecuteSession::OnSyncPhaseThreeCompleted, MakeStrong(this))
                    .Via(GetRpcInvoker()));
        }
    }

    void OnSyncPhaseThreeCompleted(const TError& error = {})
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!error.IsOK()) {
            Reply(error);
            return;
        }

        if (ContainsLocalSubrequests<EUserWorkloadType::Write>()) {
            Owner_->EnqueueReadySession<EUserWorkloadType::Write>(this);
        }

        if (ContainsLocalSubrequests<EUserWorkloadType::Read>()) {
            Owner_->EnqueueReadySession<EUserWorkloadType::Read>(this);
        }

        // NB: None of the above may be true, in which case the session is left
        // awaiting being unreferenced and destroyed.
    }

    template <EUserWorkloadType WorkloadType>
    bool ContainsLocalSubrequests()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        EExecutionSessionSubrequestType subrequestType;
        if constexpr (WorkloadType == EUserWorkloadType::Write) {
            subrequestType = EExecutionSessionSubrequestType::LocalWrite;
        } else if constexpr (WorkloadType == EUserWorkloadType::Read) {
            subrequestType = EExecutionSessionSubrequestType::LocalRead;
        } else {
            static_assert("Unexpected workload type for detecting local subrequests");
        }

        for (int index = 0; index < TotalSubrequestCount_; ++index) {
            auto& subrequest = Subrequests_[index];
            if (subrequest.Type == subrequestType) {
                return true;
            }
        }

        return false;
    }

    void ForwardRemoteRequests()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        const auto& multicellManager = Bootstrap_->GetMulticellManager();

        using TBatchKey = std::tuple<TCellTag, NHydra::EPeerKind>;
        struct TBatchValue
        {
            TObjectServiceProxy::TReqExecuteBatchBasePtr BatchReq;
            TCompactVector<int, 16> Indexes;
        };
        THashMap<TBatchKey, TErrorOr<TBatchValue>> batchMap;
        auto getOrCreateBatch = [&] (TCellTag cellTag, NHydra::EPeerKind peerKind) {
            auto key = std::tuple(cellTag, peerKind);
            auto it = batchMap.find(key);
            if (it == batchMap.end()) {
                try {
                    auto proxy = TObjectServiceProxy::FromDirectMasterChannel(
                        multicellManager->GetMasterChannelOrThrow(cellTag, peerKind));
                    // TODO(nadya02): Set the correct timeout here.
                    proxy.SetDefaultTimeout(NRpc::DefaultRpcRequestTimeout);
                    auto batchReq = proxy.ExecuteBatchNoBackoffRetries();
                    batchReq->SetOriginalRequestId(RequestId_);
                    auto reserved = false;
                    const auto& config = Owner_->Config_;
                    batchReq->SetTimeout(
                        ComputeForwardingTimeout(
                            RpcContext_->GetTimeout().value_or(config->DefaultExecuteTimeout),
                            RpcContext_->GetStartTime(),
                            config->ForwardedRequestTimeoutReserve,
                            &reserved));
                    if (!reserved) {
                        // If the timeout for forwarded request has been shortened, backoff alarm on the
                        // remote side will trigger sooner that locally, and we should deal with the local
                        // alarm as normal.
                        // Otherwise both alarms will trigger more or less simultaneously, which makes the
                        // local alarm useless at best and harmful at worst.
                        TDelayedExecutor::CancelAndClear(BackoffAlarmCookie_);
                    }
                    NRpc::SetAuthenticationIdentity(batchReq, RpcContext_->GetAuthenticationIdentity());
                    it = batchMap.emplace(key, TBatchValue{
                        .BatchReq = std::move(batchReq)
                    }).first;
                } catch (const std::exception& ex) {
                    it = batchMap.emplace(key, TError(ex)).first;
                }
            }
            return &it->second;
        };

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            if (subrequest.Type != EExecutionSessionSubrequestType::Remote) {
                continue;
            }

            subrequest.Started.store(true);

            const auto& requestHeader = subrequest.RequestHeader;
            const auto& ypathExt = *subrequest.YPathExt;
            auto peerKind = subrequest.YPathExt->mutating()
                ? NHydra::EPeerKind::Leader
                : NHydra::EPeerKind::Follower;

            auto* batchOrError = getOrCreateBatch(subrequest.ForwardedCellTag, peerKind);

            if (!batchOrError->IsOK()) {
                YT_LOG_DEBUG(
                    *batchOrError,
                    "Error forwarding subrequest (SubrequestIndex: %v, CellTag: %v, PeerKind: %v, Mutating: %v, Method: %v)",
                    subrequestIndex,
                    subrequest.ForwardedCellTag,
                    peerKind,
                    subrequest.YPathExt->mutating(),
                    requestHeader.method());
                OnCompletedSubresponse(&subrequest, CreateErrorResponseMessage(*batchOrError));
                continue;
            }

            auto& batch = batchOrError->Value();
            batch.BatchReq->AddRequestMessage(subrequest.RemoteRequestMessage);
            batch.Indexes.push_back(subrequestIndex);

            YT_LOG_DEBUG("Forwarding object request (ForwardedRequestId: %v, Method: %v.%v, "
                "%v%v%v%v, Mutating: %v%v, Retry: %v, CellTag: %v, PeerKind: %v)",
                batch.BatchReq->GetRequestId(),
                requestHeader.service(),
                requestHeader.method(),
                MakeFormatterWrapper([&] (auto* builder) {
                    if (subrequest.TargetPathRewrite) {
                        builder->AppendFormat("TargetPath: %v, ", subrequest.TargetPathRewrite);
                    }
                }),
                MakeFormatterWrapper([&] (auto* builder) {
                    if (subrequest.AdditionalPathRewrites && !subrequest.AdditionalPathRewrites->empty()) {
                        builder->AppendFormat("AdditionalPaths: %v, ", *subrequest.AdditionalPathRewrites);
                    }
                }),
                MakeFormatterWrapper([&] (auto* builder) {
                    if (subrequest.PrerequisiteRevisionPathRewrites && !subrequest.PrerequisiteRevisionPathRewrites->empty()) {
                        builder->AppendFormat("PrerequisiteRevisionPaths: %v, ", *subrequest.PrerequisiteRevisionPathRewrites);
                    }
                }),
                RpcContext_->GetAuthenticationIdentity(),
                ypathExt.mutating(),
                MakeFormatterWrapper([&] (auto* builder) {
                    if (!ypathExt.mutating()) {
                        return;
                    }
                    builder->AppendFormat(", OriginMutationId: %v",
                        NRpc::GetMutationId(subrequest.RequestHeader));
                }),
                subrequest.RequestHeader.retry(),
                subrequest.ForwardedCellTag,
                peerKind);
        }

        for (auto& [cellTag, batchReqOrError] : batchMap) {
            if (!batchReqOrError.IsOK()) {
                continue;
            }

            auto& batch = batchReqOrError.Value();
            batch.BatchReq->Invoke().Subscribe(
                // MakeWeak is sufficient - see SelfReference_.
                BIND([=, weakThis = MakeWeak(this), batch = std::move(batch)] (const TObjectServiceProxy::TErrorOrRspExecuteBatchPtr& batchRspOrError) {
                    auto this_ = weakThis.Lock();
                    if (!this_) {
                        return;
                    }

                    const auto& Logger = this_->Logger;

                    if (batchRspOrError.IsOK()) {
                        YT_LOG_DEBUG("Forwarded request succeeded (ForwardedRequestId: %v, SubrequestIndexes: %v)",
                            batch.BatchReq->GetRequestId(),
                            batch.Indexes);

                        const auto& batchRsp = batchRspOrError.Value();
                        for (auto index : batchRsp->GetUncertainRequestIndexes()) {
                            this_->MarkSubrequestAsUncertain(batch.Indexes[index]);
                        }
                        for (int index = 0; index < batchRsp->GetSize(); ++index) {
                            auto* subrequest = &this_->Subrequests_[batch.Indexes[index]];
                            if (subrequest->Uncertain.load()) {
                                continue;
                            }
                            auto responseMessage = batchRsp->GetResponseMessage(index);
                            if (responseMessage) {
                                subrequest->Revision = batchRsp->GetRevision(index);
                                this_->OnCompletedSubresponse(subrequest, std::move(responseMessage));
                            } else {
                                this_->OnMissingSubresponse(subrequest);
                            }
                        }
                    } else {
                        const auto& forwardingError = batchRspOrError;

                        YT_LOG_DEBUG(forwardingError, "Forwarded request failed (ForwardedRequestId: %v, SubrequestIndexes: %v)",
                            batch.BatchReq->GetRequestId(),
                            batch.Indexes);

                        if (!IsRetriableError(forwardingError) || forwardingError.FindMatching(NHydra::EErrorCode::ReadOnly)) {
                            YT_LOG_DEBUG(forwardingError, "Failing request due to non-retryable forwarding error (SubrequestIndexes: %v)",
                                batch.Indexes);
                            this_->Reply(TError(NObjectClient::EErrorCode::ForwardedRequestFailed, "Forwarded request failed")
                                << forwardingError);
                            return;
                        }

                        YT_LOG_DEBUG(forwardingError, "Omitting subresponses due to retryable forwarding error (SubrequestIndexes: %v)",
                            batch.Indexes);

                        for (auto index : batch.Indexes) {
                            this_->MarkSubrequestAsUncertain(index);
                        }
                    }
                }));
        }
    }

    template <EUserWorkloadType WorkloadType>
    void Reschedule()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!Enqueued<WorkloadType>().exchange(true)) {
            Owner_->EnqueueReadySession<WorkloadType>(this);
        }
    }

    template <EUserWorkloadType WorkloadType>
    void CheckAndReschedule(const TError& result)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!result.IsOK()) {
            Reply(result);
            return;
        }
        Reschedule<WorkloadType>();
    }

    template <EUserWorkloadType WorkloadType>
    bool WaitForAndContinue(const TFuture<void>& result)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (auto optionalError = result.TryGet()) {
            optionalError->ThrowOnError();
            return true;
        } else {
            result.Subscribe(
                BIND_NO_PROPAGATE(&TExecuteSession::CheckAndReschedule<WorkloadType>, MakeStrong(this)));
            return false;
        }
    }

    bool GuardedRunAutomatonFast(EUserWorkloadType workloadType)
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        Owner_->ValidateClusterInitialized();

        const auto& hydraFacade = Bootstrap_->GetHydraFacade();
        const auto& hydraManager = hydraFacade->GetHydraManager();

        hydraManager->ValidatePeer(EPeerKind::LeaderOrFollower);

        if (!EpochCancelableContext_) {
            EpochCancelableContext_ = hydraManager->GetAutomatonCancelableContext();
        }

        if (InterruptIfCanceled()) {
            return false;
        }

        if (ScheduleReplyIfNeeded()) {
            return false;
        }

        const auto& securityManager = Bootstrap_->GetSecurityManager();

        if (!User_) {
            auto* user = securityManager->GetUserByNameOrAliasOrThrow(Identity_.User, /*activeLifeStageOnly*/ true);
            User_ = TEphemeralObjectPtr<TUser>(user);

            const auto& config = Owner_->GetDynamicConfig();
            if (config->EnableReadRequestComplexityLimits && user != securityManager->GetRootUser()) {
                ReadRequestComplexityLimits_ = TReadRequestComplexityLimits{
                    .Default = config->DefaultReadRequestComplexityLimits->ToReadRequestComplexity(),
                    .Max = config->MaxReadRequestComplexityLimits->ToReadRequestComplexity(),
                };

                const auto& userConfig = user->GetObjectServiceRequestLimits();

                userConfig
                    ->DefaultReadRequestComplexityLimits
                    ->ToReadRequestComplexityOverrides()
                    .ApplyTo(ReadRequestComplexityLimits_->Default);


                userConfig
                    ->MaxReadRequestComplexityLimits
                    ->ToReadRequestComplexityOverrides()
                    .ApplyTo(ReadRequestComplexityLimits_->Max);
            }
        }

        YT_LOG_ALERT_IF(
            User_->GetPendingRemoval(),
            "User pending for removal has accessed object service (User: %v)",
            User_->GetName());

        if (NeedsUserAccessValidation_) {
            NeedsUserAccessValidation_ = false;
            auto error = securityManager->CheckUserAccess(User_.Get());
            if (!error.IsOK()) {
                Owner_->SetStickyUserError(Identity_.User, error);
                THROW_ERROR error;
            }
        }

        if (!RequestQueueSizeIncremented_) {
            if (!securityManager->TryIncrementRequestQueueSize(User_.Get())) {
                auto cellTag = Bootstrap_->GetMulticellManager()->GetCellTag();
                auto error = TError(
                    NSecurityClient::EErrorCode::RequestQueueSizeLimitExceeded,
                    "User %Qv has exceeded its request queue size limit",
                    User_->GetName())
                    << TErrorAttribute("limit", User_->GetRequestQueueSizeLimit(cellTag))
                    << TErrorAttribute("cell_tag", cellTag);
                Owner_->SetStickyUserError(Identity_.User, error);
                THROW_ERROR error;
            }
            RequestQueueSizeIncremented_ = true;
        }

        if (!ThrottleRequests(workloadType)) {
            return false;
        }

        return true;
    }

    template <EExecutionSessionSubrequestType SubrequestType, EUserWorkloadType WorkloadType>
    bool DoThrottleRequest(int* currentSubrequestIndex, int* throttledSubrequestIndex)
    {
        while (*throttledSubrequestIndex < *currentSubrequestIndex && *currentSubrequestIndex < TotalSubrequestCount_) {
            auto subrequestIndex = ++(*throttledSubrequestIndex);
            if (Subrequests_[subrequestIndex].Type != SubrequestType) {
                continue;
            }

            const auto& securityManager = Bootstrap_->GetSecurityManager();

            auto throttlerFuture = securityManager->ThrottleUser(User_.Get(), 1, WorkloadType);

            if constexpr (SubrequestType == EExecutionSessionSubrequestType::LocalWrite) {
                if (User_.Get() != securityManager->GetRootUser()) {
                    // We chain futures like this to avoid dealing with of ownership of the underlying promise of an AllSet combiner.
                    // Note that the strong ref to the session ends up somewhere within the user throttler object.
                    // Also keep in mind that we must obtain the user throttler future here, and not in a an arbitrary callback thread,
                    // because we can only dereference the user pointer from a persistent-state-read thread.
                    throttlerFuture = throttlerFuture.Apply(
                        BIND_NO_PROPAGATE([localWriteThrottlerFuture = Owner_->LocalWriteRequestThrottler_->Throttle(1)] {
                            return localWriteThrottlerFuture;
                        }));
                }
            }

            if (!WaitForAndContinue<WorkloadType>(throttlerFuture)) {
                YT_LOG_DEBUG("Throttling subrequest (User: %v, SubrequestIndex: %v, SubrequestType: %v, WorkloadType: %v)",
                    User_->GetName(),
                    subrequestIndex,
                    SubrequestType,
                    WorkloadType);
                return false;
            }
        }

        return true;
    }

    bool ThrottleRequests(EUserWorkloadType workloadType)
    {
        switch (workloadType) {
            case EUserWorkloadType::Write:
                return DoThrottleRequest<EExecutionSessionSubrequestType::LocalWrite, EUserWorkloadType::Write>(
                    &CurrentAutomatonSubrequestIndex_,
                    &ThrottledAutomatonSubrequestIndex_);
            case EUserWorkloadType::Read:
                return DoThrottleRequest<EExecutionSessionSubrequestType::LocalRead, EUserWorkloadType::Read>(
                    &CurrentLocalReadSubrequestIndex_,
                    &ThrottledLocalReadSubrequestIndex_);
            default:
                static_assert("Unexpected workload type for throttling requests");
        }
    }

    void GuardedRunAutomatonSlow()
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        GuardedProcessSubrequests(EUserWorkloadType::Write);
    }

    void GuardedRunRead()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        GuardedProcessSubrequests(EUserWorkloadType::Read);
    }

    void GuardedProcessSubrequests(EUserWorkloadType workloadType)
    {
        auto batchStartTime = GetCpuInstant();
        auto batchDeadlineTime = batchStartTime + DurationToCpuDuration(Owner_->Config_->YieldTimeout);

        Owner_->ValidateClusterInitialized();

        auto* currentSubrequestIndex = workloadType == EUserWorkloadType::Write
            ? &CurrentAutomatonSubrequestIndex_
            : &CurrentLocalReadSubrequestIndex_;

        auto filter = workloadType == EUserWorkloadType::Write
            ? [] (TSubrequest* subrequest) {
                return subrequest->Type != EExecutionSessionSubrequestType::LocalRead;
            }
            : [] (TSubrequest* subrequest) {
                return subrequest->Type == EExecutionSessionSubrequestType::LocalRead;
            };

        auto doReschedule = workloadType == EUserWorkloadType::Write
            ? [] (TExecuteSession* session) {
                session->Reschedule<EUserWorkloadType::Write>();
            }
            : [] (TExecuteSession* session) {
                session->Reschedule<EUserWorkloadType::Read>();
            };

        while (*currentSubrequestIndex < TotalSubrequestCount_) {
            // NB: PrematureBackoffAlarmProbability_ is usually std::nullopt.
            // NB: This may trigger even at 0 processed subrequests, which is intended.
            if (Y_UNLIKELY(RandomNumber<double>() <= PrematureBackoffAlarmProbability_)) {
                OnBackoffAlarm(/*premature*/ true);
            }

            if (InterruptIfCanceled()) {
                break;
            }

            if (ScheduleReplyIfNeeded()) {
                break;
            }

            if (!ThrottleRequests(workloadType)) {
                break;
            }

            if (GetCpuInstant() > batchDeadlineTime) {
                YT_LOG_DEBUG("Yielding thread");
                doReschedule(this);
                break;
            }

            {
                if (LocalExecutionInterrupted_.load()) {
                    break;
                }


                if (auto* subrequest = &Subrequests_[*currentSubrequestIndex];
                    filter(subrequest))
                {
                    ExecuteSubrequest(subrequest);
                }

                ++(*currentSubrequestIndex);
            }
        }
    }

    void ExecuteSubrequest(TSubrequest* subrequest)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
        subrequest->Revision = hydraManager->GetAutomatonVersion().ToRevision();

        switch (subrequest->Type) {
            case EExecutionSessionSubrequestType::LocalRead:
            case EExecutionSessionSubrequestType::LocalWrite:
                ExecuteLocalSubrequest(subrequest);
                break;

            case EExecutionSessionSubrequestType::Remote:
            case EExecutionSessionSubrequestType::Cache:
                break;

            default:
                YT_ABORT();
        }
    }

    void OnMutationCommitted(TSubrequest* subrequest, const TErrorOr<TMutationResponse>& responseOrError)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        YT_ASSERT(subrequest->MutationResponseFuture.IsSet());

        if (!responseOrError.IsOK()) {
            Reply(responseOrError);
            return;
        }

        const auto& response = responseOrError.Value();
        const auto& context = subrequest->RpcContext;

        if (response.Origin != EMutationResponseOrigin::Commit) {
            YT_VERIFY(!context->IsReplied());
            // Either we're answering with a kept response or this is a boomerang mutation.
            context->SetRequestInfo();
            context->SetResponseInfo("KeptResponse: %v", true);
            context->Reply(response.Data);
        }

        WaitForSubresponse(subrequest);
    }

    void ForwardSubrequestToLeader(TSubrequest* subrequest)
    {
        YT_LOG_DEBUG("Performing leader fallback");

        subrequest->ProfilingCounters->LeaderFallbackRequestCounter.Increment();

        const auto& objectManager = Bootstrap_->GetObjectManager();
        auto timeoutReserved = false;
        auto asyncSubresponse = objectManager->ForwardObjectRequest(
            subrequest->RequestMessage,
            Bootstrap_->GetMulticellManager()->GetCellTag(),
            NHydra::EPeerKind::Leader,
            &timeoutReserved);
        if (!timeoutReserved) {
            TDelayedExecutor::CancelAndClear(BackoffAlarmCookie_);
        }

        SubscribeToSubresponse(subrequest, std::move(asyncSubresponse));
    }

    void ExecuteLocalSubrequest(TSubrequest* subrequest)
    {
        switch (subrequest->Type) {
            case EExecutionSessionSubrequestType::LocalWrite:
                YT_ASSERT_THREAD_AFFINITY(AutomatonThread);
                break;
            case EExecutionSessionSubrequestType::LocalRead:
                YT_ASSERT_THREAD_AFFINITY_ANY();
                break;
            default:
                YT_ABORT();
        }

        YT_VERIFY(!subrequest->RemoteTransactionReplicationFuture || !subrequest->MutationResponseFuture);

        subrequest->Started.store(true);

        auto timeLeft = GetTimeLeft(subrequest);

        if (subrequest->RemoteTransactionReplicationSession && !subrequest->MutationResponseFuture)
        {
            YT_VERIFY(subrequest->Type == EExecutionSessionSubrequestType::LocalWrite);

            subrequest->MutationResponseFuture =
                subrequest->RemoteTransactionReplicationSession->InvokeReplicationRequestsOffloaded(timeLeft);
        }

        if (!subrequest->RemoteTransactionReplicationFuture && !subrequest->MutationResponseFuture) {
            // A local read subrequest with no remote transactions whatsoever.
            YT_VERIFY(subrequest->Type == EExecutionSessionSubrequestType::LocalRead);

            ExecuteReadSubrequest(subrequest);
            return;
        }

        if (subrequest->RemoteTransactionReplicationFuture) {
            auto onRemoteTransactionReplicated = [weakThis = MakeWeak(this), subrequest] (const TError& error) {
                auto this_ = weakThis.Lock();
                if (!this_) {
                    return;
                }

                if (!error.IsOK()) {
                    subrequest->RpcContext->Reply(error);
                } else {
                    this_->ExecuteReadSubrequest(subrequest);
                }
            };

            if (subrequest->RemoteTransactionReplicationFuture.IsSet()) {
                const auto& error = subrequest->RemoteTransactionReplicationFuture.Get();
                onRemoteTransactionReplicated(error);
            } else {
                subrequest->RemoteTransactionReplicationFuture
                    .WithTimeout(timeLeft)
                    .Subscribe(
                        BIND(onRemoteTransactionReplicated)
                            .Via(LocalReadInvoker_));
            }
        } else {
            YT_VERIFY(subrequest->MutationResponseFuture);
            if (subrequest->MutationResponseFuture.IsSet()) {
                OnMutationCommitted(subrequest, subrequest->MutationResponseFuture.Get());
            } else {
                subrequest->MutationResponseFuture
                    .Subscribe(BIND(&TExecuteSession::OnMutationCommitted, MakeStrong(this), subrequest));
            }
        }
    }

    TDuration GetTimeLeft(TSubrequest* subrequest)
    {
        const auto& requestHeader = subrequest->RequestHeader;
        auto timeout = FromProto<TDuration>(requestHeader.timeout());
        auto startTime = FromProto<TInstant>(requestHeader.start_time());
        auto now = NProfiling::GetInstant();
        return timeout - (now - startTime);
    }

    void ExecuteReadSubrequest(TSubrequest* subrequest)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        TWallTimer timer;

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        TAuthenticatedUserGuard userGuard(securityManager, User_.Get());

        const auto& rpcContext = subrequest->RpcContext;

        if (TraceContext_ && TraceContext_->IsRecorded()) {
            subrequest->TraceContext = TraceContext_->CreateChild(
                ConcatToString(TStringBuf("YPathRead:"), rpcContext->GetService(), TStringBuf("."), rpcContext->GetMethod()));
        }

        TCurrentTraceContextGuard traceContextGuard(subrequest->TraceContext);
        try {
            const auto& objectManager = Bootstrap_->GetObjectManager();
            auto rootService = objectManager->GetRootService();

            if (ReadRequestComplexityLimits_) {
                auto explicitLimits = subrequest->ReadRequestComplexityOverrides;
                explicitLimits.Validate(ReadRequestComplexityLimits_->Max);

                auto limits = ReadRequestComplexityLimits_->Default;
                subrequest->ReadRequestComplexityOverrides.ApplyTo(limits);
                auto limiter = New<TReadRequestComplexityLimiter>(limits);
                rpcContext->SetReadRequestComplexityLimiter(std::move(limiter));
            }

            {
                NConcurrency::TForbidContextSwitchGuard contextSwitchGuard;
                ExecuteVerb(rootService, rpcContext);
            }

            WaitForSubresponse(subrequest);
        } catch (const TLeaderFallbackException&) {
            ForwardSubrequestToLeader(subrequest);
        }

        // NB: Even if the user was just removed the instance is still valid but not alive.
        if (!EpochCancelableContext_->IsCanceled()) {
            securityManager->ChargeUser(User_.Get(), {EUserWorkloadType::Read, 1, timer.GetElapsedTime()});
        }
    }

    void WaitForSubresponse(TSubrequest* subrequest)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        // Optimize for the (typical) case of synchronous response.
        const auto& context = subrequest->RpcContext;
        if (context->IsReplied()) {
            OnCompletedSubresponse(subrequest, context->GetResponseMessage());
        } else {
            SubscribeToSubresponse(subrequest, context->GetAsyncResponseMessage());
        }
    }

    void SubscribeToSubresponse(TSubrequest* subrequest, TFuture<TSharedRefArray> asyncSubresponseMessage)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        asyncSubresponseMessage.Subscribe(
            BIND(&TExecuteSession::OnSubresponse, MakeStrong(this), subrequest));
    }

    void OnSubresponse(TSubrequest* subrequest, const TErrorOr<TSharedRefArray>& result)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!result.IsOK()) {
            Reply(result);
            return;
        }

        OnCompletedSubresponse(subrequest, result.Value());
    }

    void MarkSubrequestAsUncertain(int index)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        auto& subrequest = Subrequests_[index];
        subrequest.Uncertain.store(true);
        ReleaseReplyLock();
    }

    void OnCompletedSubresponse(TSubrequest* subrequest, TSharedRefArray subresponseMessage)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (subrequest->TraceContext) {
            subrequest->TraceContext->Finish();
        }

        subrequest->ResponseMessage = std::move(subresponseMessage);
        subrequest->Completed.store(true);
        SomeSubrequestCompleted_.store(true);

        // Swap out cookie before proceeding to avoid races with CancelPendingCachedSubrequests.
        if (subrequest->ActiveCacheCookieSet.exchange(false)) {
            GetRpcInvoker()->Invoke(BIND(
                &TObjectServiceCache::EndLookup,
                Owner_->Cache_,
                RequestId_,
                Passed(std::move(subrequest->ActiveCacheCookie)),
                subrequest->ResponseMessage,
                subrequest->Revision,
                /*success*/ true));
        }

        ReleaseReplyLock();
    }

    void OnMissingSubresponse(TSubrequest* subrequest)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        // Missing subresponses are only possible for remote subrequests, and
        // there should be no trace contexts for them.
        YT_ASSERT(!subrequest->TraceContext);

        YT_VERIFY(!subrequest->Uncertain.load());
        YT_VERIFY(!subrequest->Completed.load());

        ReleaseReplyLock();
    }


    void Reply(const TError& error = TError())
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        Interrupt();

        if (!ReplyScheduled_.exchange(true)) {
            TObjectService::GetRpcInvoker()
                ->Invoke(BIND(&TExecuteSession::DoReply, MakeStrong(this), error));
        }
    }

    void DoReply(const TError& error)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        // DoReply is called from a callback holding a strong-reference to this,
        // so this is just future-proofing.
        auto finally = Finally([&] { SelfReference_.Reset(); });

        TDelayedExecutor::CancelAndClear(BackoffAlarmCookie_);
        CancelPendingCacheSubrequests();

        if (RpcContext_->IsCanceled()) {
            return;
        }

        if (!error.IsOK()) {
            RpcContext_->Reply(error);
            return;
        }

        auto& response = RpcContext_->Response();
        auto& attachments = response.Attachments();

        // Will take a snapshot.
        TCompactVector<int, 16> completedIndexes;
        TCompactVector<int, 16> uncertainIndexes;

        // Check for forwarding errors.
        for (auto index = 0; index < TotalSubrequestCount_; ++index) {
            auto& subrequest = Subrequests_[index];

            if (subrequest.Uncertain) {
                uncertainIndexes.push_back(index);
                continue;
            }

            if (!subrequest.Completed) {
                if (subrequest.Started) {
                    uncertainIndexes.push_back(index);
                }

                continue;
            }

            const auto& subresponseMessage = subrequest.ResponseMessage;
            NRpc::NProto::TResponseHeader subresponseHeader;
            YT_VERIFY(TryParseResponseHeader(subresponseMessage, &subresponseHeader));

            if (subresponseHeader.error().code() == ToUnderlying(NObjectClient::EErrorCode::ForwardedRequestFailed)) {
                auto wrapperError = FromProto<TError>(subresponseHeader.error());
                YT_VERIFY(wrapperError.InnerErrors().size() == 1);

                const auto& forwardingError = wrapperError.InnerErrors()[0];
                if (!IsRetriableError(forwardingError) || forwardingError.FindMatching(NHydra::EErrorCode::ReadOnly)) {
                    YT_LOG_DEBUG(forwardingError, "Failing request due to non-retryable forwarding error (SubrequestIndex: %v)",
                        index);
                    RpcContext_->Reply(wrapperError);
                    return;
                }

                YT_LOG_DEBUG(forwardingError, "Omitting subresponse due to retryable forwarding error (SubrequestIndex: %v)",
                    index);

                uncertainIndexes.push_back(index);
            } else {
                completedIndexes.push_back(index);
            }
        }

        for (int index : completedIndexes) {
            const auto& subrequest = Subrequests_[index];
            const auto& subresponseMessage = subrequest.ResponseMessage;
            attachments.insert(attachments.end(), subresponseMessage.Begin(), subresponseMessage.End());

            auto* subresponse = response.add_subresponses();
            subresponse->set_index(index);
            subresponse->set_part_count(subresponseMessage.Size());
            subresponse->set_revision(ToProto(subrequest.Revision));
        }

        if (Owner_->EnableTwoLevelCache_) {
            response.set_caching_enabled(true);
        }

        ToProto(response.mutable_uncertain_subrequest_indexes(), uncertainIndexes);

        if (response.subresponses_size() == 0) {
            YT_LOG_DEBUG("Dropping request since no subresponses are available");
            return;
        }

        RpcContext_->SetResponseInfo("SubresponseCount: %v, UncertainSubrequestIndexes: %v",
            response.subresponses_size(),
            response.uncertain_subrequest_indexes());

        RpcContext_->Reply();
    }


    void Interrupt()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!LocalExecutionInterrupted_.exchange(true)) {
            YT_LOG_DEBUG("Request interrupted");
        }
    }

    void CancelPendingCacheSubrequests()
    {
        if (!Subrequests_) {
            return;
        }

        for (int subrequestIndex = 0; subrequestIndex < TotalSubrequestCount_; ++subrequestIndex) {
            auto& subrequest = Subrequests_[subrequestIndex];
            if (subrequest.ActiveCacheCookieSet.exchange(false)) {
                auto cookie = std::move(subrequest.ActiveCacheCookie);
                cookie.Cancel(TError(NYT::EErrorCode::Canceled, "Cache request canceled"));
            }
        }
    }

    bool InterruptIfCanceled()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (RpcContext_->IsCanceled() || EpochCancelableContext_->IsCanceled()) {
            Interrupt();
            return true;
        } else {
            return false;
        }
    }

    void ScheduleBackoffAlarm()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (!RpcContext_->Request().allow_backoff()) {
            return;
        }

        auto requestTimeout = RpcContext_->GetTimeout();
        if (requestTimeout && *requestTimeout > Owner_->Config_->TimeoutBackoffLeadTime) {
            auto backoffDelay = *requestTimeout - Owner_->Config_->TimeoutBackoffLeadTime;
            BackoffAlarmCookie_ = TDelayedExecutor::Submit(
                BIND(&TObjectService::TExecuteSession::OnBackoffAlarm, MakeWeak(this)),
                backoffDelay,
                TObjectService::GetRpcInvoker());
        }
    }

    void OnBackoffAlarm(bool premature = false)
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (Y_UNLIKELY(premature)) {
            // Premature backoff alarm is only triggered from Automaton or LocalRead threads,
            // so it's safe to read subrequest indices here.
            YT_LOG_DEBUG("Backoff alarm triggered prematurely "
                "(CurrentAutomatonSubrequestIndex: %v, CurrentLocalReadSubrequestIndex: %v, TotalSubrequestCount: %v)",
                CurrentAutomatonSubrequestIndex_,
                CurrentLocalReadSubrequestIndex_,
                TotalSubrequestCount_);
        } else {
            YT_LOG_DEBUG("Backoff alarm triggered");
        }

        BackoffAlarmTriggered_.store(true);

        ScheduleReplyIfNeeded();
    }

    bool ReleaseReplyLock()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        int result = --ReplyLockCount_;
        YT_VERIFY(result >= 0);
        YT_LOG_TRACE("Reply lock released (LockCount: %v)",
            result);
        return ScheduleReplyIfNeeded();
    }

    bool ScheduleReplyIfNeeded()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        if (ReplyScheduled_.load()) {
            return true;
        }

        if (ReplyLockCount_.load() == 0) {
            Reply();
            return true;
        }

        if (BackoffAlarmTriggered_ && SomeSubrequestCompleted_) {
            if (!LocalExecutionInterrupted_.exchange(true)) {
                YT_LOG_DEBUG("Local execution interrupted due to backoff alarm");
            }
            Reply();
            return true;
        }

        return false;
    }

    TCodicilGuard MakeCodicilGuard()
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        return TCodicilGuard(MakeNonOwningCodicilBuilder(Codicil_));
    }

    static bool GetSuppressUpstreamSync(const TCtxExecutePtr& rpcContext)
    {
        // COMPAT(shakurov): remove the former.
        return
            rpcContext->Request().suppress_upstream_sync() ||
            NObjectClient::GetSuppressUpstreamSync(rpcContext->RequestHeader());
    }

    static bool GetSuppressTransactionCoordinatorSync(const TCtxExecutePtr& rpcContext)
    {
        // COMPAT(shakurov): remove the former.
        return
            rpcContext->Request().suppress_transaction_coordinator_sync() ||
            NObjectClient::GetSuppressTransactionCoordinatorSync(rpcContext->RequestHeader());
    }
};

////////////////////////////////////////////////////////////////////////////////

TObjectService::TLocalReadCallbackProvider::TLocalReadCallbackProvider(IFairSchedulerPtr<TClosure> scheduler)
    : Scheduler_(std::move(scheduler))
{ }

TCallback<void()> TObjectService::TLocalReadCallbackProvider::ExtractCallback()
{
    auto optionalTask = Scheduler_->TryDequeue();
    return optionalTask ? *optionalTask : TClosure();
}

////////////////////////////////////////////////////////////////////////////////

const TDynamicObjectServiceConfigPtr& TObjectService::GetDynamicConfig()
{
    YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

    return Bootstrap_->GetConfigManager()->GetConfig()->ObjectService;
}

void TObjectService::OnDynamicConfigChanged(TDynamicClusterConfigPtr /*oldConfig*/)
{
    YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

    const auto& objectServiceConfig = GetDynamicConfig();
    EnableTwoLevelCache_ = objectServiceConfig->EnableTwoLevelCache;
    ScheduleReplyRetryBackoff_ = objectServiceConfig->ScheduleReplyRetryBackoff;
    MinimizeExecuteLatency_ = objectServiceConfig->MinimizeExecuteLatency;
    PrematureBackoffAlarmProbability_ = objectServiceConfig->Testing->PrematureBackoffAlarmProbability.value_or(NullPrematureBackoffAlarmProbability);

    const auto& sequoiaConfig = Bootstrap_->GetConfigManager()->GetConfig()->SequoiaManager;
    EnableCypressTransactionsInSequoia_.store(
        sequoiaConfig->Enable && sequoiaConfig->EnableCypressTransactionsInSequoia,
        std::memory_order::release);

    LocalReadExecutor_->SetThreadCount(objectServiceConfig->LocalReadThreadCount);
    LocalReadOffloadPool_->SetThreadCount(objectServiceConfig->LocalReadOffloadThreadCount);
    ProcessSessionsExecutor_->SetPeriod(objectServiceConfig->ProcessSessionsPeriod);
    LocalWriteRequestThrottler_->Reconfigure(objectServiceConfig->LocalWriteRequestThrottler);
}

template <EUserWorkloadType WorkloadType>
void TObjectService::EnqueueReadySession(TExecuteSessionPtr session)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    if constexpr (WorkloadType == EUserWorkloadType::Write) {
        AutomatonReadySessions_.Enqueue(std::move(session));
    } else if constexpr (WorkloadType == EUserWorkloadType::Read) {
        LocalReadReadySessions_.Enqueue(std::move(session));
    } else {
        static_assert("Unexpected workload type for enqueuing execute session");
    }

    if (MinimizeExecuteLatency_.load(std::memory_order::relaxed)) {
        ProcessSessionsExecutor_->ScheduleOutOfBand();
    }
}

void TObjectService::EnqueueFinishedSession(TExecuteSessionInfo sessionInfo)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    if (sessionInfo.EpochCancelableContext) {
        FinishedSessionInfos_.Enqueue(std::move(sessionInfo));
    }
}

void TObjectService::ProcessSessions()
{
    YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

    auto startTime = GetCpuInstant();
    auto deadlineTime = startTime + DurationToCpuDuration(Config_->YieldTimeout);

    FinishedSessionInfos_.DequeueAll(false, [&] (const TExecuteSessionInfo& sessionInfo) {
        FinishSession(sessionInfo);
    });

    AutomatonReadySessions_.DequeueAll(false, [&] (TExecuteSessionPtr& session) {
        TCurrentTraceContextGuard guard(session->GetTraceContext());

        session->OnDequeued<EUserWorkloadType::Write>();

        if (!session->RunAutomatonFast(EUserWorkloadType::Write)) {
            return;
        }

        const auto& userName = session->GetUserName();
        AutomatonScheduler_->Enqueue(session, userName);
    });
    LocalReadReadySessions_.DequeueAll(false, [&] (TExecuteSessionPtr& session) {
        TCurrentTraceContextGuard guard(session->GetTraceContext());

        session->OnDequeued<EUserWorkloadType::Read>();

        // TODO(shakurov): consider introducing RunLocalReadFast.
        if (!session->RunAutomatonFast(EUserWorkloadType::Read)) {
            return;
        }

        const auto& userName = session->GetUserName();
        LocalReadScheduler_->Enqueue(BIND(&TObjectService::TExecuteSession::RunRead, session), userName);
    });

    while (GetCpuInstant() < deadlineTime) {
        auto optionalSession = AutomatonScheduler_->TryDequeue();
        if (!optionalSession) {
            break;
        }

        const auto& session = *optionalSession;
        TCurrentTraceContextGuard guard(session->GetTraceContext());
        session->RunAutomatonSlow();
    }

    auto quantumDuration = GetDynamicConfig()->LocalReadExecutorQuantumDuration;

    TAutomatonBlockGuard guard(Bootstrap_->GetHydraFacade());
    auto readFuture = LocalReadExecutor_->Run(quantumDuration);

    if (Config_->EnableLocalReadBusyWait) {
        // Busy wait is intended here to account local read time
        // into automaton thread CPU usage.
        while (!readFuture.IsSet())
        { }
    }

    readFuture
        .Get()
        .ThrowOnError();
}

void TObjectService::FinishSession(const TExecuteSessionInfo& sessionInfo)
{
    YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

    if (sessionInfo.EpochCancelableContext->IsCanceled()) {
        return;
    }

    if (sessionInfo.RequestQueueSizeIncreased && IsObjectAlive(sessionInfo.User)) {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        securityManager->DecrementRequestQueueSize(sessionInfo.User.Get());
    }
}

void TObjectService::OnUserCharged(TUser* user, const TUserWorkload& workload)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto charge = [&] (const auto& scheduler) {
        scheduler->ChargeUser(user->GetName(), workload.RequestTime);
    };

    switch (workload.Type) {
        case EUserWorkloadType::Read:
            charge(LocalReadScheduler_);
            break;
        case EUserWorkloadType::Write:
            charge(AutomatonScheduler_);
            break;
        default:
            YT_ABORT();
    }
}

void TObjectService::SetStickyUserError(const std::string& userName, const TError& error)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    StickyUserErrorCache_.Put(userName, error);
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_RPC_SERVICE_METHOD(TObjectService, Execute)
{
    Y_UNUSED(response);

    const auto& userName = context->GetAuthenticationIdentity().User;
    auto error = StickyUserErrorCache_.Get(userName);
    if (!error.IsOK()) {
        context->Reply(error);
        return;
    }

    New<TExecuteSession>(this, context)->RunRpc();
}

DEFINE_RPC_SERVICE_METHOD(TObjectService, GCCollect)
{
    Y_UNUSED(request);
    Y_UNUSED(response);

    context->SetRequestInfo();

    ValidateClusterInitialized();
    ValidatePeer(EPeerKind::Leader);

    const auto& objectManager = Bootstrap_->GetObjectManager();
    context->ReplyFrom(objectManager->GCCollect());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer
