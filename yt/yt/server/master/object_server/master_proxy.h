#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

namespace NYT::NObjectServer {

////////////////////////////////////////////////////////////////////////////////

IObjectProxyPtr CreateMasterProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TMasterObject* object);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer
