#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/cypress_server/public.h>

#include <yt/yt/server/master/transaction_server/public.h>

#include <yt/yt/server/master/object_server/public.h>

namespace NYT::NTableServer {

////////////////////////////////////////////////////////////////////////////////

NCypressServer::ICypressNodeProxyPtr CreateReplicatedTableNodeProxy(
    NCellMaster::TBootstrap* bootstrap,
    NObjectServer::TObjectTypeMetadata* metadata,
    NTransactionServer::TTransaction* transaction,
    TReplicatedTableNode* trunkNode);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer

