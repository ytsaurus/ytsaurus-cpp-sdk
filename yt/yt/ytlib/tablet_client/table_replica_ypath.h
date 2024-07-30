#pragma once

#include <yt/yt/ytlib/tablet_client/proto/table_replica_ypath.pb.h>

#include <yt/yt/core/ytree/ypath_proxy.h>

namespace NYT::NTabletClient {

////////////////////////////////////////////////////////////////////////////////

struct TTableReplicaYPathProxy
    : public NYTree::TYPathProxy
{
    DEFINE_YPATH_PROXY(TableReplica);

    DEFINE_MUTATING_YPATH_PROXY_METHOD(NProto, Alter);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletClient
