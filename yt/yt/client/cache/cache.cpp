#include "cache_base.h"
#include "rpc.h"

#include <yt/yt/client/api/options.h>

#include <yt/yt/core/net/address.h>

#include <yt/yt_proto/yt/client/cache/proto/config.pb.h>

#include <util/stream/str.h>

namespace NYT::NClient::NCache {
namespace {

////////////////////////////////////////////////////////////////////////////////

TStringBuf GetNormalClusterName(TStringBuf clusterName)
{
    return NNet::InferYTClusterFromClusterUrlRaw(clusterName).value_or(clusterName);
}

TClustersConfig GetClustersConfigWithNormalClusterName(const TClustersConfig& config)
{
    TClustersConfig newConfig(config);
    newConfig.clear_cluster_configs();
    for (auto& [clusterName, clusterConfig] : config.cluster_configs()) {
        (*newConfig.mutable_cluster_configs())[ToString(GetNormalClusterName(clusterName))] = clusterConfig;
    }
    return newConfig;
}

} // namespace

TConfig MakeClusterConfig(
    const TClustersConfig& clustersConfig,
    TStringBuf clusterUrl)
{
    auto [cluster, proxyRole] = ExtractClusterAndProxyRole(clusterUrl);
    auto it = clustersConfig.cluster_configs().find(GetNormalClusterName(cluster));
    auto config = (it != clustersConfig.cluster_configs().end()) ? it->second : clustersConfig.default_config();
    config.set_cluster_name(ToString(cluster));
    if (!proxyRole.empty()) {
        config.set_proxy_role(ToString(proxyRole));
    }
    return config;
}

////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////

class TClientsCache
    : public TClientsCacheBase
{
public:
    TClientsCache(const TClustersConfig& config, const NApi::TClientOptions& options)
        : ClustersConfig_(GetClustersConfigWithNormalClusterName(config))
        , Options_(options)
    { }

protected:
    NApi::IClientPtr CreateClient(TStringBuf clusterUrl) override
    {
        return NCache::CreateClient(MakeClusterConfig(ClustersConfig_, clusterUrl), Options_);
    }

private:
    const TClustersConfig ClustersConfig_;
    const NApi::TClientOptions Options_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace

////////////////////////////////////////////////////////////////////////////////

IClientsCachePtr CreateClientsCache(const TClustersConfig& config, const NApi::TClientOptions& options)
{
    return New<TClientsCache>(config, options);
}

IClientsCachePtr CreateClientsCache(const TConfig& config, const NApi::TClientOptions& options)
{
    TClustersConfig clustersConfig;
    *clustersConfig.mutable_default_config() = config;
    return CreateClientsCache(clustersConfig, options);
}

IClientsCachePtr CreateClientsCache(const TConfig& config)
{
    return CreateClientsCache(config, NApi::GetClientOpsFromEnvStatic());
}

IClientsCachePtr CreateClientsCache(const NApi::TClientOptions& options)
{
    return CreateClientsCache(TClustersConfig(), options);
}

IClientsCachePtr CreateClientsCache()
{
    return CreateClientsCache(NApi::GetClientOpsFromEnvStatic());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClient::NCache
