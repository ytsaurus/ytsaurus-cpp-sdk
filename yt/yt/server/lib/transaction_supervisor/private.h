#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra/public.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NTransactionSupervisor {

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENTITY_TYPE(TCommit, TTransactionId, ::THash<TTransactionId>)

class TAbort;

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, TransactionSupervisorLogger, "TransactionSupervisor");
YT_DEFINE_GLOBAL(const NProfiling::TProfiler, TransactionSupervisorProfiler, "/transaction_supervisor");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionSupervisor
