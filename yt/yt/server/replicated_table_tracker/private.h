#pragma once

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NReplicatedTableTracker {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, ReplicatedTableTrackerLogger, "StandaloneRtt");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, ReplicatedTableTrackerProfiler, "/standalone_rtt");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NReplicatedTableTracker
