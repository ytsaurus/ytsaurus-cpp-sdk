#include "config.h"
#include "public.h"

#include <yt/yt/core/misc/config.h>

namespace NYT::NAuth {

////////////////////////////////////////////////////////////////////////////////

void TNativeAuthenticationManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("tvm_service", &TThis::TvmService)
        .Default();
    registrar.Parameter("enable_validation", &TThis::EnableValidation)
        .Default(false);
    registrar.Parameter("enable_submission", &TThis::EnableSubmission)
        .Default(true);

    registrar.Postprocessor([] (TThis* config) {
        if (config->EnableValidation && !config->EnableSubmission) {
            THROW_ERROR_EXCEPTION("Turning off validation while submission is enabled results in non-working configuration");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TNativeAuthenticationManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable_validation", &TThis::EnableValidation)
        .Default();
    registrar.Parameter("enable_submission", &TThis::EnableSubmission)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TTvmBridgeConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("self_tvm_id", &TThis::SelfTvmId)
        .Default(0);
    registrar.Parameter("refresh_period", &TThis::RefreshPeriod)
        .Default(TDuration::Hours(1));
    registrar.Parameter("ensure_tickets_backoff", &TThis::EnsureTicketsBackoff)
        .Default({
            .InvocationCount = 20,
        });
    registrar.Parameter("rpc_timeout", &TThis::RpcTimeout)
        .Default(TDuration::Seconds(10));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NAuth
