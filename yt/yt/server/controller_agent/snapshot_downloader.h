#pragma once

#include "private.h"

#include <yt/yt/core/logging/log.h>

#include <library/cpp/yt/memory/ref.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

class TSnapshotDownloader
    : public TRefCounted
{
public:
    TSnapshotDownloader(
        TControllerAgentConfigPtr config,
        TBootstrap* bootstrap,
        TOperationId operationId);

    std::vector<TSharedRef> Run();

private:
    const TControllerAgentConfigPtr Config_;
    TBootstrap* const Bootstrap_;
    const TOperationId OperationId_;

    const NLogging::TLogger Logger;

};

DEFINE_REFCOUNTED_TYPE(TSnapshotDownloader)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
