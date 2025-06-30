#pragma once

#include "public.h"
#include "member.h"

#include <util/generic/set.h>

#include <yt/yt/core/ytree/attributes.h>

#include <yt/yt/ytlib/discovery_client/helpers.h>

#include <yt/yt/server/lib/discovery_server/helpers.h>

namespace NYT::NDiscoveryServer {

////////////////////////////////////////////////////////////////////////////////

class TGroup
    : public TRefCounted
{
public:
    TGroup(
        const TGroupId& id,
        TClosure onGroupEmpty,
        const NLogging::TLogger& Logger);

    const TGroupId& GetId();
    int GetMemberCount();

    TMemberPtr AddOrUpdateMember(
        const NDiscoveryClient::TMemberInfo& memberInfo,
        const TGroupManagerInfo& groupManagerInfo,
        TDuration leaseTimeout,
        bool respectLimits);
    std::vector<TMemberPtr> ListMembers(std::optional<int> limit = std::nullopt);
    bool HasMember(const TMemberId& memberId);
    TMemberPtr FindMember(const TMemberId& memberId);
    TMemberPtr GetMemberOrThrow(const TMemberId& memberId);

private:
    const TGroupId Id_;
    const TClosure OnGroupEmptied_;
    const NLogging::TLogger Logger;

    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, MembersLock_);
    TSet<TMemberPtr, TMemberPtrComparer> Members_;
    THashMap<TMemberId, TMemberPtr> IdToMember_;

    TMemberPtr AddMember(const NDiscoveryClient::TMemberInfo& memberInfo, TDuration leaseTimeout);
    TMemberPtr UpdateMember(const NDiscoveryClient::TMemberInfo& memberInfo, TDuration leaseTimeout);
    void UpdateMemberInfo(const TMemberPtr& member, const NDiscoveryClient::TMemberInfo& memberInfo, TDuration leaseTimeout);
};

DEFINE_REFCOUNTED_TYPE(TGroup)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDiscoveryServer
