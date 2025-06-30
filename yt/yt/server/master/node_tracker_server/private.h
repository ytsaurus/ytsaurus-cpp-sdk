#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NNodeTrackerServer {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, NodeTrackerServerStructuredLogger, "NodeTrackerServerStructured");
YT_DEFINE_GLOBAL(const NLogging::TLogger, NodeTrackerServerLogger, "NodeTrackerServer");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, NodeTrackerProfiler, "/node_tracker");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
