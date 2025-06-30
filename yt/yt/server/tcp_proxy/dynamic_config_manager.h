#pragma once

#include "public.h"
#include "config.h"

#include <yt/yt/library/dynamic_config/dynamic_config_manager.h>

namespace NYT::NTcpProxy {

////////////////////////////////////////////////////////////////////////////////

//! Manages dynamic configuration of a TCP Proxy
//! by pulling it periodically from masters.
/*!
 *  \note
 *  Thread affinity: any
 */
class TDynamicConfigManager
    : public NDynamicConfig::TDynamicConfigManagerBase<TProxyDynamicConfig>
{
public:
    explicit TDynamicConfigManager(IBootstrap* bootstrap);

    std::vector<std::string> GetInstanceTags() const override;

private:
    const std::vector<std::string> InstanceTags_;
};

DEFINE_REFCOUNTED_TYPE(TDynamicConfigManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTcpProxy
