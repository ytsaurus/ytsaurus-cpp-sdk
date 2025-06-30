#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NCellarNode {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, CellarNodeLogger, "CellarNode");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, CellarNodeProfiler, "/cellar_node");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellarNode
