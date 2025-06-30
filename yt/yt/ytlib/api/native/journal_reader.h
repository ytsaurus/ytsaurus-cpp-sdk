#pragma once

#include "public.h"

#include <yt/yt/client/api/client.h>

#include <yt/yt/core/ypath/public.h>

namespace NYT::NApi::NNative {

////////////////////////////////////////////////////////////////////////////////

IJournalReaderPtr CreateJournalReader(
    IClientPtr client,
    const NYPath::TYPath& path,
    const TJournalReaderOptions& options = TJournalReaderOptions());

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative

