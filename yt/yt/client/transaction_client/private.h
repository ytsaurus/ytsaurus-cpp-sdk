#pragma once

#include <yt/yt/core/logging/log.h>

namespace NYT::NTransactionClient {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, TransactionClientLogger, "TransactionClient");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionClient