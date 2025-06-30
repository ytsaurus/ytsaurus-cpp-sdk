#pragma once

#include "public.h"

#include <yt/yt/server/lib/hive/public.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

//! Redirects mutations to the target servant of a tablet that participates
//! in smooth movement.
struct IMutationForwarder
    : public TRefCounted
{
    virtual void MaybeForwardMutationToSiblingServant(
        const TTablet* tablet,
        const ::google::protobuf::Message& message) = 0;

    virtual void MaybeForwardMutationToSiblingServant(
        TTabletId tabletId,
        const ::google::protobuf::Message& message) = 0;
};

DEFINE_REFCOUNTED_TYPE(IMutationForwarder)

////////////////////////////////////////////////////////////////////////////////

IMutationForwarderPtr CreateMutationForwarder(
    TWeakPtr<ITabletManager> tabletManager,
    NHiveServer::IHiveManagerPtr hiveManager);

IMutationForwarderPtr CreateDummyMutationForwarder();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
