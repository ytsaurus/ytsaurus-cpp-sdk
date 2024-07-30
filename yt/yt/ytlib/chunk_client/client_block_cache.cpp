#include "client_block_cache.h"
#include "private.h"
#include "block_cache.h"
#include "config.h"
#include "block_id.h"

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/misc/async_slru_cache.h>
#include <yt/yt/core/misc/config.h>
#include <yt/yt/core/misc/memory_usage_tracker.h>
#include <yt/yt/core/misc/property.h>

namespace NYT::NChunkClient {

using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static constexpr auto& Logger = ChunkClientLogger;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TAsyncBlockCacheEntry)

class TAsyncBlockCacheEntry
    : public TAsyncCacheValueBase<TBlockId, TAsyncBlockCacheEntry>
{
public:
    DEFINE_BYREF_RO_PROPERTY(TCachedBlock, CachedBlock);

public:
    TAsyncBlockCacheEntry(TBlockId id, TCachedBlock cachedBlock)
        : TAsyncCacheValueBase(id)
        , CachedBlock_(std::move(cachedBlock))
    { }
};

DEFINE_REFCOUNTED_TYPE(TAsyncBlockCacheEntry)

////////////////////////////////////////////////////////////////////////////////

TCachedBlock PrepareBlockToCache(TCachedBlock block, const IMemoryUsageTrackerPtr& tracker)
{
    block.Data = TrackMemory(tracker, std::move(block.Data));
    return block;
}

////////////////////////////////////////////////////////////////////////////////

class TCachedBlockCookie
    : public ICachedBlockCookie
{
public:
    using TAsyncCacheCookie = TAsyncSlruCacheBase<TBlockId, TAsyncBlockCacheEntry>::TInsertCookie;

public:
    explicit TCachedBlockCookie(
        TAsyncCacheCookie cookie,
        IMemoryUsageTrackerPtr memoryUsageTracker)
        : MemoryUsageTracker_(std::move(memoryUsageTracker))
        , Cookie_(std::move(cookie))
    { }

    bool IsActive() const override
    {
        return Cookie_.IsActive();
    }

    TFuture<void> GetBlockFuture() const override
    {
        return Cookie_.GetValue().AsVoid();
    }

    TCachedBlock GetBlock() const override
    {
        const auto& future = Cookie_.GetValue();
        YT_VERIFY(future.IsSet() && future.Get().IsOK());
        return future.Get().Value()->CachedBlock();
    }

    void SetBlock(TErrorOr<TCachedBlock> blockOrError) override
    {
        if (BlockSet_.exchange(true)) {
            return;
        }

        if (blockOrError.IsOK()) {
            auto block = PrepareBlockToCache(std::move(blockOrError).Value(), MemoryUsageTracker_);
            auto entry = New<TAsyncBlockCacheEntry>(Cookie_.GetKey(), std::move(block));
            Cookie_.EndInsert(std::move(entry));
        } else {
            Cookie_.Cancel(static_cast<TError>(blockOrError));
        }
    }

private:
    const IMemoryUsageTrackerPtr MemoryUsageTracker_;

    TAsyncCacheCookie Cookie_;
    std::atomic<bool> BlockSet_ = false;
};

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TPerTypeClientBlockCache)

class TPerTypeClientBlockCache
    : public TAsyncSlruCacheBase<TBlockId, TAsyncBlockCacheEntry>
{
public:
    TPerTypeClientBlockCache(
        EBlockType type,
        TSlruCacheConfigPtr config,
        const NProfiling::TProfiler& profiler,
        IMemoryUsageTrackerPtr memoryUsageTracker)
        : TAsyncSlruCacheBase(
            std::move(config),
            profiler)
        , Type_(type)
        , MemoryUsageTracker_(std::move(memoryUsageTracker))
    { }

    void PutBlock(const TBlockId& id, const TBlock& block)
    {
        if (!IsEnabled()) {
            return;
        }

        auto cookie = BeginInsert(id);
        if (cookie.IsActive()) {
            auto entry = New<TAsyncBlockCacheEntry>(id, TCachedBlock(block));
            cookie.EndInsert(std::move(entry));

            YT_LOG_DEBUG("Block is put into cache (BlockId: %v, BlockType: %v, BlockSize: %v)",
                id,
                Type_,
                block.Size());
        } else {
            // Already have the block cached, do nothing.
            YT_LOG_TRACE("Block is already in cache (BlockId: %v, BlockType: %v)",
                id,
                Type_);
        }
    }

    TCachedBlock FindBlock(const TBlockId& id)
    {
        if (!IsEnabled()) {
            return {};
        }

        auto block = TAsyncSlruCacheBase::Find(id);
        if (block) {
            YT_LOG_TRACE("Block cache hit (BlockId: %v, BlockType: %v)",
                id,
                Type_);
            return block->CachedBlock();
        } else {
            YT_LOG_TRACE("Block cache miss (BlockId: %v, BlockType: %v)",
                id,
                Type_);
            return {};
        }
    }

    std::unique_ptr<ICachedBlockCookie> GetBlockCookie(
        const TBlockId& id,
        EBlockType type)
    {
        YT_VERIFY(type == Type_);

        if (!IsEnabled()) {
            return CreateActiveCachedBlockCookie();
        }

        auto cookie = BeginInsert(id);
        return std::make_unique<TCachedBlockCookie>(std::move(cookie), MemoryUsageTracker_);
    }

    bool IsBlockTypeActive(EBlockType type) const
    {
        return type == Type_ && IsEnabled();
    }

private:
    const EBlockType Type_;
    const IMemoryUsageTrackerPtr MemoryUsageTracker_;

    i64 GetWeight(const TAsyncBlockCacheEntryPtr& entry) const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return entry->CachedBlock().Size();
    }

    bool IsEnabled() const
    {
        return GetCapacity() != 0;
    }
};

DEFINE_REFCOUNTED_TYPE(TPerTypeClientBlockCache)

////////////////////////////////////////////////////////////////////////////////

class TClientBlockCache
    : public IClientBlockCache
{
public:
    TClientBlockCache(
        TBlockCacheConfigPtr config,
        EBlockType supportedBlockTypes,
        IMemoryUsageTrackerPtr memoryTracker,
        const NProfiling::TProfiler& profiler)
        : MemoryUsageTracker_(std::move(memoryTracker))
        , SupportedBlockTypes_(supportedBlockTypes)
    {
        i64 capacity = 0;
        auto initType = [&] (EBlockType type, TSlruCacheConfigPtr config) {
            if (Any(SupportedBlockTypes_ & type)) {
                auto cache = New<TPerTypeClientBlockCache>(
                    type,
                    config,
                    profiler.WithPrefix("/" + FormatEnum(type)),
                    MemoryUsageTracker_);
                EmplaceOrCrash(PerTypeCaches_, type, cache);
                capacity += cache->GetCapacity();
            } else {
                EmplaceOrCrash(PerTypeCaches_, type, nullptr);
            }
        };
        initType(EBlockType::CompressedData, config->CompressedData);
        initType(EBlockType::UncompressedData, config->UncompressedData);
        initType(EBlockType::HashTableChunkIndex, config->HashTableChunkIndex);
        initType(EBlockType::XorFilter, config->XorFilter);
        initType(EBlockType::ChunkFragmentsData, config->ChunkFragmentsData);

        // NB: We simply override the limit as underlying per-type caches are unaware of this cascading structure.
        MemoryUsageTracker_->SetLimit(capacity);
    }

    void PutBlock(
        const TBlockId& id,
        EBlockType type,
        const TBlock& block) override
    {
        if (const auto& cache = GetOrCrash(PerTypeCaches_, type)) {
            auto cachingBlock = PrepareBlockToCache(block, MemoryUsageTracker_);
            cache->PutBlock(id, std::move(cachingBlock));
        }
    }

    TCachedBlock FindBlock(
        const TBlockId& id,
        EBlockType type) override
    {
        if (const auto& cache = GetOrCrash(PerTypeCaches_, type)) {
            return cache->FindBlock(id);
        } else {
            return TCachedBlock();
        }
    }

    std::unique_ptr<ICachedBlockCookie> GetBlockCookie(
        const TBlockId& id,
        EBlockType type) override
    {
        const auto& cache = GetOrCrash(PerTypeCaches_, type);
        return cache
            ? cache->GetBlockCookie(id, type)
            : CreateActiveCachedBlockCookie();
    }

    EBlockType GetSupportedBlockTypes() const override
    {
        return SupportedBlockTypes_;
    }

    bool IsBlockTypeActive(EBlockType type) const override
    {
        const auto& cache = GetOrCrash(PerTypeCaches_, type);
        return cache
            ? cache->IsBlockTypeActive(type)
            : false;
    }

    void Reconfigure(const TBlockCacheDynamicConfigPtr& config) override
    {
        i64 newCapacity = 0;
        auto reconfigureType = [&] (EBlockType type, TSlruCacheDynamicConfigPtr config) {
            if (const auto& cache = GetOrCrash(PerTypeCaches_, type)) {
                cache->Reconfigure(config);
                newCapacity += cache->GetCapacity();
            }
        };
        reconfigureType(EBlockType::CompressedData, config->CompressedData);
        reconfigureType(EBlockType::UncompressedData, config->UncompressedData);
        reconfigureType(EBlockType::HashTableChunkIndex, config->HashTableChunkIndex);
        reconfigureType(EBlockType::XorFilter, config->XorFilter);
        reconfigureType(EBlockType::ChunkFragmentsData, config->ChunkFragmentsData);

        // NB: We simply override the limit as underlying per-type caches know nothing about this cascading structure.
        MemoryUsageTracker_->SetLimit(newCapacity);
    }

private:
    const IMemoryUsageTrackerPtr MemoryUsageTracker_;

    const EBlockType SupportedBlockTypes_;

    TCompactFlatMap<EBlockType, TPerTypeClientBlockCachePtr, TEnumTraits<EBlockType>::GetDomainSize()> PerTypeCaches_;
};

////////////////////////////////////////////////////////////////////////////////

IClientBlockCachePtr CreateClientBlockCache(
    TBlockCacheConfigPtr config,
    EBlockType supportedBlockTypes,
    IMemoryUsageTrackerPtr memoryUsageTracker,
    const NProfiling::TProfiler& profiler)
{
    YT_VERIFY(memoryUsageTracker);
    return New<TClientBlockCache>(
        std::move(config),
        supportedBlockTypes,
        std::move(memoryUsageTracker),
        profiler);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
