#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra/public.h>

#include <yt/yt/server/master/object_server/public.h>

#include <yt/yt/server/master/cell_master/public.h>

namespace NYT::NTabletServer {

////////////////////////////////////////////////////////////////////////////////

NObjectServer::IObjectTypeHandlerPtr CreateTabletCellBundleTypeHandler(
    NCellMaster::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
