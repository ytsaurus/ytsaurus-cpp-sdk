#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NTabletServer {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TTabletTracker)

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, TabletServerLogger, "TabletServer");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, TabletServerProfiler, "/tablet_server");

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TReqUpdateTabletStores;
class TReqUpdateHunkTabletStores;

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
