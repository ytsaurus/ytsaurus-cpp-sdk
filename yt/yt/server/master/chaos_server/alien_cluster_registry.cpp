#include "alien_cluster_registry.h"

#include <yt/yt/server/master/cell_master/serialize.h>

namespace NYT::NChaosServer {

using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

int TAlienClusterRegistry::GetOrRegisterAlienClusterIndex(const std::string& clusterName)
{
    if (auto it = NameToIndex_.find(clusterName)) {
        return it->second;
    }

    int alienClusterIndex = IndexToName_.size();
    IndexToName_.push_back(clusterName);
    YT_VERIFY(NameToIndex_.emplace(clusterName, alienClusterIndex).second);
    return alienClusterIndex;
}

const std::string& TAlienClusterRegistry::GetAlienClusterName(int alienClusterIndex) const
{
    YT_VERIFY(alienClusterIndex < std::ssize(IndexToName_));
    return IndexToName_[alienClusterIndex];
}

void TAlienClusterRegistry::Clear()
{
    NameToIndex_.clear();
    IndexToName_.clear();
}

void TAlienClusterRegistry::Save(TSaveContext& context) const
{
    using NYT::Save;

    Save(context, IndexToName_);
}

void TAlienClusterRegistry::Load(TLoadContext& context)
{
    using NYT::Load;

    auto indexToName = Load<std::vector<std::string>>(context);

    Reset(std::move(indexToName));
}

const std::vector<std::string>& TAlienClusterRegistry::GetIndexToName() const
{
    return IndexToName_;
}

void TAlienClusterRegistry::Reset(std::vector<std::string> indexToName)
{
    IndexToName_ = std::move(indexToName);

    for (int alienClusterIndex = 0; alienClusterIndex < std::ssize(IndexToName_); ++alienClusterIndex) {
        YT_VERIFY(NameToIndex_.emplace(IndexToName_[alienClusterIndex], alienClusterIndex).second);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChaosServer
