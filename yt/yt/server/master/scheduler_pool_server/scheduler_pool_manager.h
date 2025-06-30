#pragma once

#include "public.h"

#include "scheduler_pool.h"

namespace NYT::NSchedulerPoolServer {

////////////////////////////////////////////////////////////////////////////////

struct ISchedulerPoolManager
    : public virtual TRefCounted
{
public:
    virtual void Initialize() = 0;

    virtual TSchedulerPool* CreateSchedulerPool() = 0;

    virtual TSchedulerPool* FindSchedulerPoolByName(const std::string& treeName, const std::string& name) const = 0;
    virtual TSchedulerPoolTree* FindPoolTreeObjectByName(const std::string& treeName) const = 0;
    virtual TSchedulerPool* FindPoolTreeOrSchedulerPoolOrThrow(const std::string& treeName, const std::string& name) const = 0;

    virtual const THashMap<std::string, TSchedulerPoolTree*>& GetPoolTrees() const = 0;

    virtual const THashSet<NYTree::TInternedAttributeKey>& GetKnownPoolAttributes() = 0;
    virtual const THashSet<NYTree::TInternedAttributeKey>& GetKnownPoolTreeAttributes() = 0;
    virtual bool IsUserManagedAttribute(NYTree::TInternedAttributeKey key) = 0;

    // Pool tree name is obtained from root object.
    // It has complexity linear in the object's depth since we have to traverse all parents all the way to the root.
    virtual std::optional<std::string> GetMaybePoolTreeName(const TSchedulerPool* schedulerPool) noexcept = 0;

    virtual void TransferPoolResources(
        TSchedulerPool* srcPool,
        TSchedulerPool* dstPool,
        const TPoolResourcesPtr& resourceDelta) = 0;
};

DEFINE_REFCOUNTED_TYPE(ISchedulerPoolManager)

////////////////////////////////////////////////////////////////////////////////

ISchedulerPoolManagerPtr CreateSchedulerPoolManager(NCellMaster::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSchedulerPoolServer
