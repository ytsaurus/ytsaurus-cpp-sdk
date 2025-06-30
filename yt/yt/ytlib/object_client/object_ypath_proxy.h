#pragma once

#include <yt/yt/ytlib/object_client/proto/object_ypath.pb.h>

#include <yt/yt/core/ytree/ypath_proxy.h>

namespace NYT::NObjectClient {

////////////////////////////////////////////////////////////////////////////////

struct TObjectYPathProxy
    : public NYTree::TYPathProxy
{
    DEFINE_YPATH_PROXY(Object);

    DEFINE_YPATH_PROXY_METHOD(NProto, GetBasicAttributes);
    DEFINE_YPATH_PROXY_METHOD(NProto, CheckPermission);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectClient
