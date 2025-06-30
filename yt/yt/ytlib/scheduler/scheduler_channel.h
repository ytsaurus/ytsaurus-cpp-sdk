#pragma once

#include "public.h"

#include <yt/yt/core/rpc/public.h>
#include <yt/yt/ytlib/node_tracker_client/public.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

//! Creates a channel pointing to the scheduler of a given cell.
NRpc::IChannelPtr CreateSchedulerChannel(
    TSchedulerConnectionConfigPtr config,
    NRpc::IChannelFactoryPtr channelFactory,
    NRpc::IChannelPtr masterChannel,
    const NNodeTrackerClient::TNetworkPreferenceList& networks);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
