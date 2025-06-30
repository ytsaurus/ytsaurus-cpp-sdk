#pragma once

#include "public.h"

#include <yt/yt/server/lib/chunk_server/proto/job_tracker_service.pb.h>

#include <yt/yt/core/rpc/client.h>

namespace NYT::NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TJobTrackerServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TJobTrackerServiceProxy, JobTrackerService,
        .SetProtocolVersion(17)
        .SetAcceptsBaggage(false));

    DEFINE_RPC_PROXY_METHOD(NProto, Heartbeat);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
