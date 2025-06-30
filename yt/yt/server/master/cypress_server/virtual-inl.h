#ifndef VIRTUAL_INL_H_
#error "Direct inclusion of this file is not allowed, include virtual.h"
// For the sake of sane code completion.
#include "virtual.h"
#endif

#include <yt/yt/server/master/cypress_server/lock.h>

#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/server/master/object_server/object.h>
#include <yt/yt/server/master/object_server/object_manager.h>

#include <yt/yt/core/misc/collection_helpers.h>

#include <yt/yt/core/ytree/virtual.h>

namespace NYT::NCypressServer {

////////////////////////////////////////////////////////////////////////////////

template <class TValue>
class TVirtualObjectMap
    : public NYTree::TVirtualMapBase
{
public:
    TVirtualObjectMap(
        NCellMaster::TBootstrap* bootstrap,
        const NHydra::TReadOnlyEntityMap<TValue>* map,
        NYTree::INodePtr owningNode)
        : TVirtualMapBase(owningNode)
        , Bootstrap_(bootstrap)
        , Map_(map)
    { }

protected:
    NCellMaster::TBootstrap* const Bootstrap_;
    const NHydra::TReadOnlyEntityMap<TValue>* const Map_;

    std::vector<std::string> GetKeys(i64 limit) const override
    {
        return ConvertToStrings(NYT::GetKeys(*Map_, limit));
    }

    i64 GetSize() const override
    {
        return Map_->GetSize();
    }

    NYTree::IYPathServicePtr FindItemService(const std::string& key) const override
    {
        auto id = NHydra::TEntityKey<TValue>::FromString(key);
        auto* object = Map_->Find(id);
        if (!NObjectServer::IsObjectAlive(object)) {
            return nullptr;
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(object);
    }
};

template <class TValue>
NYTree::IYPathServicePtr CreateVirtualObjectMap(
    NCellMaster::TBootstrap* bootstrap,
    const NHydra::TReadOnlyEntityMap<TValue>& map,
    NYTree::INodePtr owningNode)
{
    return New<TVirtualObjectMap<TValue>>(
        bootstrap,
        &map,
        owningNode);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer
