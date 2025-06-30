#ifndef GOSSIP_VALUE_INL_H_
#error "Direct inclusion of this file is not allowed, include gossip_value.h"
// For the sake of sane code completion.
#include "gossip_value.h"
#endif

#include "bootstrap.h"
#include "config.h"
#include "config_manager.h"

namespace NYT::NCellMaster {

////////////////////////////////////////////////////////////////////////////////

template <class TValue>
TGossipValue<TValue>::TGossipValue() = default;

template <class TValue>
TValue& TGossipValue<TValue>::Local() noexcept
{
    return *LocalPtr_;
}

template <class TValue>
const TValue& TGossipValue<TValue>::Local() const noexcept
{
    return *LocalPtr_;
}

template <class TValue>
TValue* TGossipValue<TValue>::Remote(NObjectClient::TCellTag cellTag)
{
    return &GetOrCrash(Multicell(), cellTag);
}

template <class TValue>
void TGossipValue<TValue>::Initialize(TBootstrap* bootstrap)
{
    auto cellTag = bootstrap->GetCellTag();
    const auto& secondaryCellTags = bootstrap->GetSecondaryCellTags();

    if (secondaryCellTags.empty()) {
        SetLocalPtr(&Cluster());
    } else {
        auto& multicellStatistics = Multicell();
        if (multicellStatistics.find(cellTag) == multicellStatistics.end()) {
            multicellStatistics[cellTag] = Cluster();
        }

        for (auto secondaryCellTag : secondaryCellTags) {
            multicellStatistics[secondaryCellTag];
        }

        for (auto it = multicellStatistics.begin(); it != multicellStatistics.end();) {
            auto masterCellTag = it->first;
            if (!secondaryCellTags.contains(masterCellTag) && bootstrap->GetPrimaryCellTag() != masterCellTag) {
                YT_VERIFY(bootstrap->GetConfigManager()->GetConfig()->MulticellManager->Testing->AllowMasterCellRemoval);
                multicellStatistics.erase(it++);
            } else {
                ++it;
            }
        }

        SetLocalPtr(&multicellStatistics[cellTag]);
    }
}

template <class TValue>
void TGossipValue<TValue>::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Cluster_);
    Persist(context, Multicell_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMaster
