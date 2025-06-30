#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/object_server/public.h>

#include <yt/yt/server/lib/hydra/public.h>

namespace NYT::NNodeTrackerServer {

////////////////////////////////////////////////////////////////////////////////

NObjectServer::IObjectTypeHandlerPtr CreateDataCenterTypeHandler(
    NCellMaster::TBootstrap* bootstrap,
    NHydra::TEntityMap<TDataCenter>* map);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
