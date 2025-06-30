#pragma once

#include "public.h"

#include <yt/yt/library/profiling/sensor.h>

#include <yt/yt/core/logging/log.h>

namespace NYT::NHiveServer {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, HiveServerLogger, "HiveServer");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, HiveServerProfiler, "/hive");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveServer
