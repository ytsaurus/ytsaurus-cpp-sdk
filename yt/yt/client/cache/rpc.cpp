#include "rpc.h"

#include <yt/yt_proto/yt/client/cache/proto/config.pb.h>

#include <yt/yt/client/api/client.h>

#include <yt/yt/client/api/options.h>

#include <yt/yt/client/api/rpc_proxy/config.h>
#include <yt/yt/client/api/rpc_proxy/connection.h>

#include <util/string/strip.h>

#include <util/system/env.h>

namespace NYT::NClient::NCache {

////////////////////////////////////////////////////////////////////////////////

NCompression::ECodec GetCompressionCodecFromProto(ECompressionCodec protoCodec)
{
    switch (protoCodec) {
        case ECompressionCodec::None:
            return NCompression::ECodec::None;
        case ECompressionCodec::Lz4:
            return NCompression::ECodec::Lz4;
    }
    YT_ABORT();
}

NApi::NRpcProxy::TConnectionConfigPtr GetConnectionConfig(const TConfig& config)
{
    auto connectionConfig = New<NApi::NRpcProxy::TConnectionConfig>();
    connectionConfig->SetDefaults();

    connectionConfig->ClusterUrl = config.cluster_name();
    if (!config.proxy_role().empty()) {
        connectionConfig->ProxyRole = config.proxy_role();
    }
    if (config.channel_pool_size() != 0) {
        connectionConfig->DynamicChannelPool->MaxPeerCount = config.channel_pool_size();
    }
    if (config.channel_pool_rebalance_interval_seconds() != 0) {
        connectionConfig->DynamicChannelPool->RandomPeerEvictionPeriod = TDuration::Seconds(config.channel_pool_rebalance_interval_seconds());
    }
    if (config.has_enable_power_of_two_choices_strategy()) {
        connectionConfig->DynamicChannelPool->EnablePowerOfTwoChoicesStrategy = config.enable_power_of_two_choices_strategy();
    }
    if (config.modify_rows_batch_capacity() != 0) {
        connectionConfig->ModifyRowsBatchCapacity = config.modify_rows_batch_capacity();
    }
    if (config.has_enable_proxy_discovery()) {
        connectionConfig->EnableProxyDiscovery = config.enable_proxy_discovery();
    }
    if (!config.proxy_addresses().empty()) {
        connectionConfig->ProxyAddresses = std::vector<TString>(config.proxy_addresses().begin(), config.proxy_addresses().end());
    }

#define SET_TIMEOUT_OPTION(snakeName, pascalName) \
    if (config.snakeName() != 0) connectionConfig->pascalName = TDuration::MilliSeconds(config.snakeName())

    SET_TIMEOUT_OPTION(default_transaction_timeout, DefaultTransactionTimeout);
    SET_TIMEOUT_OPTION(default_select_rows_timeout, DefaultSelectRowsTimeout);
    SET_TIMEOUT_OPTION(default_lookup_rows_timeout, DefaultLookupRowsTimeout);
    SET_TIMEOUT_OPTION(default_total_streaming_timeout, DefaultTotalStreamingTimeout);
    SET_TIMEOUT_OPTION(default_streaming_stall_timeout, DefaultStreamingStallTimeout);
    SET_TIMEOUT_OPTION(default_ping_period, DefaultPingPeriod);

#undef SET_TIMEOUT_OPTION

    connectionConfig->RequestCodec = GetCompressionCodecFromProto(config.request_codec());
    connectionConfig->ResponseCodec = GetCompressionCodecFromProto(config.response_codec());
    connectionConfig->EnableRetries = config.enable_retries();

    if (config.has_enable_legacy_rpc_codecs()) {
        connectionConfig->EnableLegacyRpcCodecs = config.enable_legacy_rpc_codecs();
    }
    if (config.has_enable_select_query_tracing_tag()) {
        connectionConfig->EnableSelectQueryTracingTag = config.enable_select_query_tracing_tag();
    }
    if (config.has_retry_backoff_time()) {
        connectionConfig->RetryingChannel->RetryBackoffTime = TDuration::MilliSeconds(config.retry_backoff_time());
    }
    if (config.has_retry_attempts()) {
        connectionConfig->RetryingChannel->RetryAttempts = config.retry_attempts();
    }
    if (config.has_retry_timeout()) {
        connectionConfig->RetryingChannel->RetryTimeout = TDuration::MilliSeconds(config.retry_timeout());
    }

    if (config.has_cluster_tag()) {
        connectionConfig->ClusterTag = NApi::TClusterTag(config.cluster_tag());
    }

    if (config.has_clock_cluster_tag()) {
        connectionConfig->ClockClusterTag = NObjectClient::TCellTag(config.clock_cluster_tag());
    }

    if (config.has_udf_registry_path()) {
        connectionConfig->UdfRegistryPath = config.udf_registry_path();
    }

    connectionConfig->Postprocess();

    return connectionConfig;
}

////////////////////////////////////////////////////////////////////////////////

std::pair<TStringBuf, TStringBuf> ExtractClusterAndProxyRole(TStringBuf clusterUrl)
{
    static const TStringBuf schemeDelim = "://";

    auto startPos = clusterUrl.find(schemeDelim);
    if (startPos != TStringBuf::npos) {
        startPos += schemeDelim.size();
    } else {
        startPos = 0;
    }

    auto endPos = clusterUrl.rfind('/');
    if (endPos != TStringBuf::npos && endPos > startPos) {
        return {clusterUrl.Head(endPos), clusterUrl.Tail(endPos + 1)};
    } else {
        return {clusterUrl, ""};
    }
}

void SetClusterUrl(const NApi::NRpcProxy::TConnectionConfigPtr& config, TStringBuf clusterUrl)
{
    auto [cluster, proxyRole] = ExtractClusterAndProxyRole(clusterUrl);
    if (!proxyRole.empty()) {
        Y_ENSURE(!config->ProxyRole || config->ProxyRole.value().empty(), "ProxyRole specified in both: config and url");
        config->ProxyRole = ToString(proxyRole);
    }
    config->ClusterUrl = ToString(cluster);
}

void SetClusterUrl(TConfig& config, TStringBuf clusterUrl)
{
    auto [cluster, proxyRole] = ExtractClusterAndProxyRole(clusterUrl);
    if (!proxyRole.empty()) {
        Y_ENSURE(config.proxy_role().empty(), "ProxyRole specified in both: config and url");
        config.set_proxy_role(ToString(proxyRole));
    }
    config.set_cluster_name(ToString(cluster));
}

NApi::IClientPtr CreateClient(const NApi::NRpcProxy::TConnectionConfigPtr& config, const NApi::TClientOptions& options)
{
    return NApi::NRpcProxy::CreateConnection(config)->CreateClient(options);
}

NApi::IClientPtr CreateClient(const TConfig& config, const NApi::TClientOptions& options)
{
    return CreateClient(GetConnectionConfig(config), options);
}

NApi::IClientPtr CreateClient(const NApi::NRpcProxy::TConnectionConfigPtr& config)
{
    return CreateClient(config, NApi::GetClientOpsFromEnvStatic());
}

NApi::IClientPtr CreateClient(const TConfig& config)
{
    return CreateClient(GetConnectionConfig(config));
}

NApi::IClientPtr CreateClient(TStringBuf clusterUrl)
{
    return CreateClient(clusterUrl, NApi::GetClientOpsFromEnvStatic());
}

NApi::IClientPtr CreateClient(TStringBuf cluster, TStringBuf proxyRole)
{
    auto config = New<NApi::NRpcProxy::TConnectionConfig>();
    config->ClusterUrl = ToString(cluster);
    if (!proxyRole.empty()) {
        config->ProxyRole = ToString(proxyRole);
    }
    return CreateClient(config);
}

NApi::IClientPtr CreateClient()
{
    return CreateClient(Strip(GetEnv("YT_PROXY")));
}

NApi::IClientPtr CreateClient(TStringBuf clusterUrl, const NApi::TClientOptions& options)
{
    TConfig config;
    SetClusterUrl(config, clusterUrl);
    return CreateClient(config, options);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClient::NCache
