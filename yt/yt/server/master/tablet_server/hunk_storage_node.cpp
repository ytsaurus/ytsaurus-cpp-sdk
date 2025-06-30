#include "hunk_storage_node.h"

#include <yt/yt/server/master/cypress_server/cypress_manager.h>

#include <yt/yt/server/master/table_server/table_node.h>

namespace NYT::NTabletServer {

using namespace NCellMaster;
using namespace NCypressClient;
using namespace NTableServer;

////////////////////////////////////////////////////////////////////////////////

std::string THunkStorageNode::GetLowercaseObjectName() const
{
    return Format("hunk storage %v", GetId());
}

std::string THunkStorageNode::GetCapitalizedObjectName() const
{
    return Format("Hunk storage %v", GetId());
}

void THunkStorageNode::Save(TSaveContext& context) const
{
    TBase::Save(context);

    using NYT::Save;

    Save(context, ReadQuorum_);
    Save(context, WriteQuorum_);

    Save(context, AssociatedNodeIds_);
}

void THunkStorageNode::Load(TLoadContext& context)
{
    TBase::Load(context);

    using NYT::Load;

    Load(context, ReadQuorum_);
    Load(context, WriteQuorum_);
    Load(context, AssociatedNodeIds_);
}

void THunkStorageNode::ValidateRemount() const
{
    THROW_ERROR_EXCEPTION("Hunk storage does not support remount");
}

void THunkStorageNode::ValidateFreeze() const
{
    THROW_ERROR_EXCEPTION("Hunk storage does not support freeze");
}

void THunkStorageNode::ValidateUnfreeze() const
{
    THROW_ERROR_EXCEPTION("Hunk storage does not support unfreeze");
}

void THunkStorageNode::ValidateReshard(
    const TBootstrap* bootstrap,
    int firstTabletIndex,
    int lastTabletIndex,
    int newTabletCount,
    const std::vector<NTableClient::TLegacyOwningKey>& pivotKeys,
    const std::vector<i64>& trimmedRowCounts) const
{
    TTabletOwnerBase::ValidateReshard(
        bootstrap,
        firstTabletIndex,
        lastTabletIndex,
        newTabletCount,
        pivotKeys,
        trimmedRowCounts);

    if (!trimmedRowCounts.empty()) {
        THROW_ERROR_EXCEPTION("Cannot reshard hunk storage node with \"trimmed_row_counts\"");
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
