#include "timestamp_manager.h"
#include "private.h"
#include "config.h"

#include <yt/yt/server/lib/election/election_manager.h>

#include <yt/yt/server/lib/hydra/composite_automaton.h>
#include <yt/yt/server/lib/hydra/hydra_manager.h>
#include <yt/yt/server/lib/hydra/mutation.h>
#include <yt/yt/server/lib/hydra/serialize.h>

#include <yt/yt/server/lib/timestamp_server/proto/timestamp_manager.pb.h>

#include <yt/yt/client/transaction_client/timestamp_service_proxy.h>
#include <yt/yt/client/transaction_client/helpers.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/actions/cancelable_context.h>

#include <yt/yt/core/misc/serialize.h>

#include <yt/yt/core/rpc/server.h>
#include <yt/yt/core/rpc/service_detail.h>

#include <time.h>

namespace NYT::NTimestampServer {

using namespace NRpc;
using namespace NHydra;
using namespace NTransactionClient;
using namespace NConcurrency;
using namespace NObjectClient;
using namespace NTimestampServer::NProto;

////////////////////////////////////////////////////////////////////////////////

class TTimestampManager::TImpl
    : public TServiceBase
    , public TCompositeAutomatonPart
{
public:
    TImpl(
        TTimestampManagerConfigPtr config,
        IInvokerPtr automatonInvoker,
        IHydraManagerPtr hydraManager,
        TCompositeAutomatonPtr automaton,
        TCellTag cellTag,
        IAuthenticatorPtr authenticator,
        NObjectClient::TCellTag clockClusterTag)
        : TServiceBase(
            // Ignored, method handlers use TimestampInvoker_.
            GetSyncInvoker(),
            TTimestampServiceProxy::GetDescriptor(),
            TimestampServerLogger(),
            TServiceOptions{
                .Authenticator = std::move(authenticator),
            })
        , TCompositeAutomatonPart(
            hydraManager,
            automaton,
            automatonInvoker)
        , Config_(std::move(config))
        , CellTag_(cellTag)
        , ClockClusterTag_(clockClusterTag)
        , TimestampQueue_(New<TActionQueue>("Timestamp"))
        , TimestampInvoker_(TimestampQueue_->GetInvoker())
        , CalibrationExecutor_(New<TPeriodicExecutor>(
            TimestampInvoker_,
            BIND(&TImpl::Calibrate, Unretained(this)),
            Config_->CalibrationPeriod))
    {
        YT_VERIFY(Config_);
        YT_VERIFY(AutomatonInvoker_);

        YT_ASSERT_INVOKER_THREAD_AFFINITY(TimestampInvoker_, TimestampThread);
        YT_ASSERT_INVOKER_THREAD_AFFINITY(AutomatonInvoker_, AutomatonThread);

        CalibrationExecutor_->Start();

        TServiceBase::RegisterMethod(RPC_SERVICE_METHOD_DESC(GenerateTimestamps)
            .SetInvoker(TimestampInvoker_));

        DeclareServerFeature(ETimestampProviderFeature::AlienClocks);

        RegisterLoader(
            "TimestampManager",
            BIND_NO_PROPAGATE(&TImpl::Load, Unretained(this)));
        RegisterSaver(
            ESyncSerializationPriority::Values,
            "TimestampManager",
            BIND_NO_PROPAGATE(&TImpl::Save, Unretained(this)));

        TCompositeAutomatonPart::RegisterMethod(
            BIND_NO_PROPAGATE(&TImpl::HydraCommitTimestamp, Unretained(this)));
    }

    IServicePtr GetRpcService()
    {
        return this;
    }

private:
    const TTimestampManagerConfigPtr Config_;
    const TCellTag CellTag_;
    const TCellTag ClockClusterTag_;

    const TActionQueuePtr TimestampQueue_;
    const IInvokerPtr TimestampInvoker_;

    const TPeriodicExecutorPtr CalibrationExecutor_;

    // Timestamp thread affinity:

    //! Can we generate timestamps?
    std::atomic<bool> Active_ = false;

    //! Are we backing off because no committed timestamps are available?
    //! Used to avoid repeating same logging message.
    bool BackingOff_ = false;

    //! First unused timestamp.
    TTimestamp CurrentTimestamp_ = NullTimestamp;

    //! Last committed timestamp as viewed by the timestamp thread.
    //! All generated timestamps must be less than this one.
    TTimestamp CommittedTimestamp_ = NullTimestamp;


    // Automaton thread affinity:

    //! Last committed timestamp as viewed by the automaton.
    TTimestamp PersistentTimestamp_;


    DECLARE_THREAD_AFFINITY_SLOT(TimestampThread);
    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);


    // RPC handlers.

    DECLARE_RPC_SERVICE_METHOD(NTransactionClient::NProto, GenerateTimestamps)
    {
        YT_ASSERT_THREAD_AFFINITY(TimestampThread);

        context->SetRequestInfo("Count: %v", request->count());

        DoGenerateTimestamps(context);
    }

    void DoGenerateTimestamps(const TCtxGenerateTimestampsPtr& context)
    {
        YT_ASSERT_THREAD_AFFINITY(TimestampThread);

        if (!Active_.load()) {
            context->Reply(TError(
                NRpc::EErrorCode::Unavailable,
                "Timestamp provider is not active"));
            return;
        }

        const auto& request = context->Request();

        int count = request.count();
        YT_VERIFY(count >= 0);

        auto requestClockClusterTag = request.has_clock_cluster_tag()
            ? FromProto<TCellTag>(request.clock_cluster_tag())
            : InvalidCellTag;

        if (ClockClusterTag_ != InvalidCellTag && requestClockClusterTag != InvalidCellTag &&
            ClockClusterTag_ != requestClockClusterTag)
        {
            context->Reply(TError(
                NTransactionClient::EErrorCode::ClockClusterTagMismatch,
                "Different clock cluster tag")
                << TErrorAttribute("clock_cluster_tag", ClockClusterTag_)
                << TErrorAttribute("request_clock_cluster_tag", requestClockClusterTag));
            return;
        }

        if (count > Config_->MaxTimestampsPerRequest) {
            context->Reply(TError("Too many timestamps requested: %v > %v",
                count,
                Config_->MaxTimestampsPerRequest));
            return;
        }

        bool enoughSpareTimestamps = CurrentTimestamp_ + count < CommittedTimestamp_;
        if (Config_->EmbedCellTag) {
            enoughSpareTimestamps &= CanAdvanceTimestampWithEmbeddedCellTag(CurrentTimestamp_, count);
        }

        if (!enoughSpareTimestamps) {
            // Backoff and retry.
            YT_LOG_WARNING_UNLESS(BackingOff_, "Not enough spare timestamps; backing off");
            BackingOff_ = true;
            TDelayedExecutor::Submit(
                BIND(&TImpl::DoGenerateTimestamps, MakeStrong(this), context)
                    .Via(TimestampInvoker_),
                Config_->RequestBackoffTime);
            return;
        }

        if (BackingOff_) {
            YT_LOG_INFO("Spare timestamps are available again");
            BackingOff_ = false;
        }

        auto result = CurrentTimestamp_;
        CurrentTimestamp_ += count;

        context->SetResponseInfo("Timestamp: %v", result);

        context->Response().set_timestamp(result);
        if (ClockClusterTag_ != InvalidCellTag) {
            context->Response().set_clock_cluster_tag(ToProto(ClockClusterTag_));
        }
        context->Reply();
    }

    static ui64 GetCurrentUnixTime()
    {
        return ::time(nullptr);
    }

    void Calibrate()
    {
        YT_ASSERT_THREAD_AFFINITY(TimestampThread);

        if (!Active_.load()) {
            return;
        }

        ui64 clockUnixTime = GetCurrentUnixTime();
        ui64 currentTimestampUnixTime = UnixTimeFromTimestamp(CurrentTimestamp_);

        if (clockUnixTime == currentTimestampUnixTime) {
            return;
        }

        if (clockUnixTime < currentTimestampUnixTime) {
            YT_LOG_WARNING("Clock went back, keeping current timestamp (CurrentTimestampUnixTime: %v, ClockUnixTime: %v)",
                currentTimestampUnixTime,
                clockUnixTime);
            return;
        }

        YT_VERIFY(clockUnixTime > currentTimestampUnixTime);

        ui64 newCurrentTimestamp = TimestampFromUnixTime(clockUnixTime);
        if (Config_->EmbedCellTag) {
            newCurrentTimestamp = EmbedCellTagIntoTimestamp(newCurrentTimestamp, CellTag_);
        }

        // NB: The check is for sanity.
        if (newCurrentTimestamp > CurrentTimestamp_) {
            CurrentTimestamp_ = newCurrentTimestamp;
        }

        auto proposedTimestamp = TimestampFromUnixTime(clockUnixTime + Config_->TimestampPreallocationInterval.Seconds());

        YT_LOG_DEBUG("Timestamp calibrated (CurrentTimestamp: %v, ProposedTimestamp: %v)",
            CurrentTimestamp_,
            proposedTimestamp);

        TReqCommitTimestamp request;
        request.set_timestamp(proposedTimestamp);

        auto mutation = CreateMutation(HydraManager_, request);
        BIND([mutation = std::move(mutation)] {
            return mutation->Commit();
        })
            .AsyncVia(AutomatonInvoker_)
            .Run()
            .Subscribe(BIND(&TImpl::OnTimestampCommitted, MakeStrong(this), proposedTimestamp)
                .Via(TimestampInvoker_));
    }

    void OnTimestampCommitted(TTimestamp timestamp, const TErrorOr<TMutationResponse>& result)
    {
        YT_ASSERT_THREAD_AFFINITY(TimestampThread);

        if (!result.IsOK()) {
            YT_LOG_ERROR(result, "Error committing timestamp");
            return;
        }

        CommittedTimestamp_ = std::max(CommittedTimestamp_, timestamp);

        YT_LOG_DEBUG("Timestamp committed (CommittedTimestamp: %v)",
            CommittedTimestamp_);
    }


    void Clear() override
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        TCompositeAutomatonPart::Clear();

        PersistentTimestamp_ = NullTimestamp;
    }

    void Load(TLoadContext& context)
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        NYT::Load(context, PersistentTimestamp_);
    }

    void Save(TSaveContext& context) const
    {
        NYT::Save(context, PersistentTimestamp_);
    }


    void OnLeaderActive() override
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        TCompositeAutomatonPart::OnLeaderActive();

        YT_LOG_INFO("Activating timestamp generator (PersistentTimestamp: %v)",
            PersistentTimestamp_);

        auto persistentTimestamp = PersistentTimestamp_;
        auto invoker = HydraManager_
            ->GetAutomatonCancelableContext()
            ->CreateInvoker(TimestampInvoker_);

        auto callback = BIND([=, this, this_ = MakeStrong(this)] {
            YT_ASSERT_THREAD_AFFINITY(TimestampThread);

            Active_.store(true);
            CurrentTimestamp_ = persistentTimestamp;
            CommittedTimestamp_ = persistentTimestamp;

            if (Config_->EmbedCellTag) {
                CurrentTimestamp_ = EmbedCellTagIntoTimestamp(CurrentTimestamp_, CellTag_);
            }

            YT_LOG_INFO("Timestamp generator is now active (PersistentTimestamp: %v)",
                persistentTimestamp);
        }).Via(invoker);

        ui64 deadlineTime = UnixTimeFromTimestamp(PersistentTimestamp_);
        ui64 currentTime = GetCurrentUnixTime();
        if (currentTime > deadlineTime) {
            callback.Run();
        } else {
            auto delay = TDuration::Seconds(deadlineTime - currentTime + 1); // +1 to be sure
            YT_LOG_INFO("Timestamp generator postponed to ensure monotonicity (Delay: %v)",
                delay);
            TDelayedExecutor::Submit(callback, delay);
        }
    }

    void OnStopLeading() override
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        TCompositeAutomatonPart::OnStopLeading();

        TimestampInvoker_->Invoke(BIND([=, this, this_ = MakeStrong(this)] {
            YT_ASSERT_THREAD_AFFINITY(TimestampThread);

            if (!Active_.load()) {
                return;
            }

            Active_.store(false);
            CurrentTimestamp_ = NullTimestamp;
            CommittedTimestamp_ = NullTimestamp;

            YT_LOG_INFO("Timestamp generator is no longer active");
        }));
    }


    void HydraCommitTimestamp(TReqCommitTimestamp* request)
    {
        YT_ASSERT_THREAD_AFFINITY(AutomatonThread);

        PersistentTimestamp_ = request->timestamp();

        YT_LOG_DEBUG("Persistent timestamp updated (Timestamp: %v)",
            PersistentTimestamp_);
    }


    bool IsUp(const TCtxDiscoverPtr& /*context*/) override
    {
        YT_ASSERT_THREAD_AFFINITY_ANY();

        return Active_;
    }
};

////////////////////////////////////////////////////////////////////////////////

TTimestampManager::TTimestampManager(
    TTimestampManagerConfigPtr config,
    IInvokerPtr automatonInvoker,
    IHydraManagerPtr hydraManager,
    TCompositeAutomatonPtr automaton,
    TCellTag cellTag,
    IAuthenticatorPtr authenticator,
    NObjectClient::TCellTag clockClusterTag)
    : Impl_(New<TImpl>(
        config,
        automatonInvoker,
        hydraManager,
        automaton,
        cellTag,
        std::move(authenticator),
        clockClusterTag))
{ }

TTimestampManager::~TTimestampManager() = default;

IServicePtr TTimestampManager::GetRpcService()
{
    return Impl_->GetRpcService();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTimestampServer
