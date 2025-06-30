#pragma once

#include "public.h"
#include "master_memory_limits.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/chunk_server/public.h>

#include <yt/yt/client/object_client/public.h>

#include <yt/yt/core/misc/public.h>

namespace NYT::NSecurityServer {

////////////////////////////////////////////////////////////////////////////////

void ValidateDiskSpace(i64 diskSpace);

template <class T>
THashMap<std::string, T> CellTagMapToCellNameMap(
    const THashMap<NObjectClient::TCellTag, T>& map,
    const NCellMaster::IMulticellManagerPtr& multicellManager);

template <class T>
THashMap<NObjectClient::TCellTag, T> CellNameMapToCellTagMapOrThrow(
    const THashMap<std::string, T>& map,
    const NCellMaster::IMulticellManagerPtr& multicellManager);

////////////////////////////////////////////////////////////////////////////////

i64 GetOptionalNonNegativeI64ChildOrThrow(
    const NYTree::IMapNodePtr& mapNode,
    const char* key);

////////////////////////////////////////////////////////////////////////////////

TLimit32 GetOptionalLimit32ChildOrThrow(
    const NYTree::IMapNodePtr& mapNode,
    const char* key,
    TLimit32 defaultValue = TLimit32(0));

TLimit64 GetOptionalLimit64ChildOrThrow(
    const NYTree::IMapNodePtr& mapNode,
    const char* key,
    TLimit64 defaultValue = TLimit64(0));

////////////////////////////////////////////////////////////////////////////////

void LogAcdUpdate(
    const std::string& attribute,
    const NYPath::TYPath& path,
    const NYson::TYsonString& value);

////////////////////////////////////////////////////////////////////////////////

//! Validates modification for those attributes which can be set only by superusers.
void ValidateSuperuserOnAttributeModification(
    const ISecurityManagerPtr& securityManager,
    TStringBuf attribute);

////////////////////////////////////////////////////////////////////////////////

int GetAccountShardIndex(TAccountId accountId);

int GetAccountProfilingBucketIndex(TAccountId accountId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer

#define HELPERS_INL_H_
#include "helpers-inl.h"
#undef HELPERS_INL_H_
