#pragma once

#include "public.h"

#include <yt/yt/ytlib/chunk_client/chunk_service_proxy.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/rpc/public.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

using TChunkLocatedHandler = TCallback<void(
    TChunkId chunkId,
    const TChunkReplicaWithMediumList& replicas,
    bool missing)>;

//! A chunk scraper for unavailable chunks.
class TChunkScraper
    : public TRefCounted
{
public:
    TChunkScraper(
        const TChunkScraperConfigPtr config,
        const IInvokerPtr invoker,
        TThrottlerManagerPtr throttlerManager,
        NApi::NNative::IClientPtr client,
        NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
        const THashSet<TChunkId>& chunkIds,
        TChunkLocatedHandler onChunkLocated,
        const NLogging::TLogger& logger);

    //! Starts periodic polling.
    /*!
     *  Should be called when operation preparation is complete.
     *  Safe to call multiple times.
     */
    void Start();

    //! Stops periodic polling.
    TFuture<void> Stop();

private:
    const TChunkScraperConfigPtr Config_;
    const IInvokerPtr Invoker_;
    const TThrottlerManagerPtr ThrottlerManager_;
    const NApi::NNative::IClientPtr Client_;
    const NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory_;
    const TChunkLocatedHandler OnChunkLocated_;
    const NLogging::TLogger Logger;

    std::vector<TScraperTaskPtr> ScraperTasks_;

    void DoStart();
    TFuture<void> DoStop();

    //! Create scraper tasks for each cell.
    void CreateTasks(const THashSet<TChunkId>& chunkIds);
};

DEFINE_REFCOUNTED_TYPE(TChunkScraper)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
