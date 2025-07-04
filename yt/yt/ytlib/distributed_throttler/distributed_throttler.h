#pragma once

#include "public.h"

#include <yt/yt/core/actions/invoker.h>

#include <yt/yt/core/rpc/channel.h>

#include <yt/yt/core/concurrency/public.h>

#include <yt/yt/ytlib/discovery_client/public.h>

#include <yt/yt/ytlib/api/native/public.h>

namespace NYT::NDistributedThrottler {

////////////////////////////////////////////////////////////////////////////////

struct TThrottlerUsage
{
    double Rate = 0.0;
    double Limit = 0.0;
    i64 QueueTotalAmount = 0;
    TDuration MaxEstimatedOverdraftDuration = TDuration::Zero();
    TDuration MinEstimatedOverdraftDuration = TDuration::Max();
};

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TThrottlerToGlobalUsage)

class TThrottlerToGlobalUsage final
    : public THashMap<TThrottlerId, TThrottlerUsage>
{ };

DEFINE_REFCOUNTED_TYPE(TThrottlerToGlobalUsage)

////////////////////////////////////////////////////////////////////////////////

struct IDistributedThrottlerFactory
    : public virtual TRefCounted
{
    virtual NConcurrency::IReconfigurableThroughputThrottlerPtr GetOrCreateThrottler(
        const TThrottlerId& throttlerId,
        NConcurrency::TThroughputThrottlerConfigPtr throttlerConfig,
        TDuration throttleRpcTimeout = DefaultThrottleRpcTimeout) = 0;

    virtual void Reconfigure(TDistributedThrottlerConfigPtr config) = 0;

    //! Only the leader has non-empty throttler usages collected over all members.
    virtual TIntrusivePtr<const TThrottlerToGlobalUsage> TryGetThrottlerToGlobalUsage() const = 0;

    //! It is possible to provide throttler total limits from the outside. It is
    //! vital for leader since leader may not know total limits for some throttlers
    //! and thus skip them during member limits computations. It is important to
    //! update total limits on all nodes since leader can change any time.
    virtual void UpdateTotalLimits(
        THashMap<TThrottlerId, std::optional<double>> throttlerIdToTotalLimit) = 0;
};

DEFINE_REFCOUNTED_TYPE(IDistributedThrottlerFactory)

IDistributedThrottlerFactoryPtr CreateDistributedThrottlerFactory(
    TDistributedThrottlerConfigPtr config,
    NRpc::IChannelFactoryPtr channelFactory,
    NApi::NNative::IConnectionPtr connection,
    IInvokerPtr invoker,
    NDiscoveryClient::TGroupId groupId,
    NDiscoveryClient::TMemberId memberId,
    NRpc::IServerPtr rpcServer,
    std::string address,
    NLogging::TLogger logger,
    NRpc::IAuthenticatorPtr authenticator,
    NProfiling::TProfiler profiler = {});

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDistributedThrottler
