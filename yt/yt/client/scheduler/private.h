#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, SchedulerLogger, "Scheduler");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
