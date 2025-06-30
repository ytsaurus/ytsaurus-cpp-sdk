#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NClusterClock {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, ClusterClockLogger, "Clock");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, ClusterClockProfiler, "/clock");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClusterClock
