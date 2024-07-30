#pragma once

#include "public.h"

#include <yt/yt/ytlib/job_prober_client/proto/job_prober_service.pb.h>

#include <yt/yt/core/rpc/client.h>

namespace NYT::NJobProberClient {

////////////////////////////////////////////////////////////////////////////////

class TJobProberServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TJobProberServiceProxy, JobProberService,
        .SetProtocolVersion(0));

    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, DumpInputContext);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, GetStderr);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, GetFailContext);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, GetSpec);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, PollJobShell);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, Interrupt);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, Fail);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, Abort);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, DumpSensors);
    DEFINE_RPC_PROXY_METHOD(NJobProberClient::NProto, DumpJobProxyLog);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProberClient
