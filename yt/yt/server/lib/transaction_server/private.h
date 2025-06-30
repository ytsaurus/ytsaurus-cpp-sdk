#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NTransactionServer {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, TransactionServerLogger, "TransactionServer");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, TransactionServerProfiler, "/transaction_server");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionServer

