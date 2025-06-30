#include "tablet_type_handler.h"
#include "table_replica_proxy.h"
#include "table_replica.h"
#include "tablet_manager.h"

#include <yt/yt/server/master/object_server/type_handler_detail.h>

#include <yt/yt/server/master/cypress_server/cypress_manager.h>

#include <yt/yt/server/master/table_server/replicated_table_node.h>

#include <yt/yt/server/master/security_server/security_manager.h>

#include <yt/yt/client/object_client/helpers.h>

namespace NYT::NTabletServer {

using namespace NHydra;
using namespace NYTree;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NCypressServer;
using namespace NTableServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TTableReplicaTypeHandler
    : public TObjectTypeHandlerWithMapBase<TTableReplica>
{
public:
    TTableReplicaTypeHandler(
        TBootstrap* bootstrap,
        TEntityMap<TTableReplica>* map)
        : TObjectTypeHandlerWithMapBase(bootstrap, map)
        , Bootstrap_(bootstrap)
    { }

    EObjectType GetType() const override
    {
        return EObjectType::TableReplica;
    }

    ETypeFlags GetFlags() const override
    {
        return
            ETypeFlags::Creatable |
            ETypeFlags::Removable;
    }

    TObject* CreateObject(
        TObjectId /*hintId*/,
        IAttributeDictionary* attributes) override
    {
        auto tablePath = attributes->GetAndRemove<TYPath>("table_path");
        auto clusterName = attributes->GetAndRemove<std::string>("cluster_name");
        auto replicaPath = attributes->GetAndRemove<TYPath>("replica_path");
        auto startReplicationTimestamp = attributes->GetAndRemove<NTransactionClient::TTimestamp>("start_replication_timestamp", NTransactionClient::MinTimestamp);
        auto startReplicationRowIndexes = attributes->FindAndRemove<std::vector<i64>>("start_replication_row_indexes");
        auto mode = attributes->GetAndRemove<ETableReplicaMode>("mode", ETableReplicaMode::Async);
        auto preserveTimestamps = attributes->GetAndRemove<bool>("preserve_timestamps", true);
        auto atomicity = attributes->GetAndRemove<NTransactionClient::EAtomicity>("atomicity", NTransactionClient::EAtomicity::Full);
        auto enabled = attributes->GetAndRemove<bool>("enabled", false);
        auto enableReplicatedTableTracker = attributes->GetAndRemove<bool>("enable_replicated_table_tracker", true);

        const auto& cypressManager = Bootstrap_->GetCypressManager();
        auto* trunkNode = cypressManager->ResolvePathToTrunkNode(tablePath);
        if (trunkNode->GetType() != EObjectType::ReplicatedTable) {
            THROW_ERROR_EXCEPTION("%v is not a replicated table",
                tablePath);
        }
        auto* table = trunkNode->As<TReplicatedTableNode>();

        if (startReplicationRowIndexes && startReplicationRowIndexes->size() != table->Tablets().size()) {
            THROW_ERROR_EXCEPTION("Invalid size of \"start_replication_row_indexes\": expected zero or %v, got %v",
                table->Tablets().size(),
                startReplicationRowIndexes->size());
        }

        table->ValidateNotBackup("Cannot create table replica from a backup table");

        cypressManager->LockNode(table, nullptr, ELockMode::Exclusive);

        const auto& tabletManager = Bootstrap_->GetTabletManager();
        return tabletManager->CreateTableReplica(
            table,
            clusterName,
            replicaPath,
            mode,
            preserveTimestamps,
            atomicity,
            enabled,
            startReplicationTimestamp,
            startReplicationRowIndexes,
            enableReplicatedTableTracker);
    }

private:
    TBootstrap* const Bootstrap_;

    IObjectProxyPtr DoGetProxy(TTableReplica* replica, TTransaction* /*transaction*/) override
    {
        return CreateTableReplicaProxy(Bootstrap_, &Metadata_, replica);
    }

    void DoZombifyObject(TTableReplica* replica) override
    {
        TObjectTypeHandlerWithMapBase::DoZombifyObject(replica);

        const auto& tabletManager = Bootstrap_->GetTabletManager();
        tabletManager->ZombifyTableReplica(replica);
    }
};

////////////////////////////////////////////////////////////////////////////////

IObjectTypeHandlerPtr CreateTableReplicaTypeHandler(
    TBootstrap* bootstrap,
    TEntityMap<TTableReplica>* map)
{
    return New<TTableReplicaTypeHandler>(bootstrap, map);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
