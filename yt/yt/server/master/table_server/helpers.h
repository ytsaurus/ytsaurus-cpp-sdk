#pragma once

#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/library/heavy_schema_validation/schema_validation.h>

#include <yt/yt/server/master/table_server/table_node.h>

namespace NYT::NTableServer {

////////////////////////////////////////////////////////////////////////////////

//! Return the stage stored in the node if it is set and the per-cluster default if it is null.
std::string GetEffectiveQueueAgentStage(
    NCellMaster::TBootstrap* bootstrap,
    const std::optional<std::string>& queueAgentStage);

//! For use with EInternedAttributeKey::[Queue/Consumer][Status/Partitions].
TFuture<NYson::TYsonString> GetQueueAgentAttributeAsync(
    NCellMaster::TBootstrap* bootstrap,
    const std::optional<std::string>& queueAgentStage,
    const NYPath::TYPath& path,
    NYTree::TInternedAttributeKey key);

NTableClient::TSchemaUpdateEnabledFeatures GetSchemaUpdateEnabledFeatures(
    NCellMaster::TDynamicClusterConfigPtr config);

void RecomputeTabletStatistics(TTableNode* table);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer
