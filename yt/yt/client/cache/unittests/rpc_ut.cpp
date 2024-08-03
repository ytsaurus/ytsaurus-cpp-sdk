#include <yt/yt/client/cache/rpc.h>

#include <library/cpp/testing/gtest/gtest.h>

namespace NYT::NClient::NCache {

////////////////////////////////////////////////////////////////////////////////

TEST(RpcClientTest, SetClusterUrlWithoutProxy)
{
    TConfig config;
    SetClusterUrl(config, "markov");
    EXPECT_EQ("markov", config.cluster_name());
    EXPECT_EQ("", config.proxy_role());
}

TEST(RpcClientTest, SetClusterUrlWithProxy)
{
    TConfig config;
    SetClusterUrl(config, "markov/bigb");
    EXPECT_EQ("markov", config.cluster_name());
    EXPECT_EQ("bigb", config.proxy_role());
}

TEST(RpcClientTest, SetClusterUrlFqdnWithoutProxy)
{
    TConfig config;
    SetClusterUrl(config, "https://markov.yt.yandex.net:443");
    EXPECT_EQ("https://markov.yt.yandex.net:443", config.cluster_name());
    EXPECT_EQ("", config.proxy_role());
}

TEST(RpcClientTest, SetClusterUrlFqdnWithProxy)
{
    TConfig config;
    SetClusterUrl(config, "https://markov.yt.yandex.net:443/bigb");
    EXPECT_EQ("https://markov.yt.yandex.net:443", config.cluster_name());
    EXPECT_EQ("bigb", config.proxy_role());
}

TEST(RpcClientTest, ProxyRoleOverride)
{
    TConfig config;
    config.set_proxy_role("role");
    EXPECT_THROW(SetClusterUrl(config, "markov/bigb"), std::exception);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClient::NCache
