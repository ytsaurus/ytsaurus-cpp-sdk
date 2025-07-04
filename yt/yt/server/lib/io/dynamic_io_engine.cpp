#include "dynamic_io_engine.h"

#include "io_engine.h"

#include <yt/yt/core/misc/fair_share_hierarchical_queue.h>

#include <library/cpp/yt/containers/enum_indexed_array.h>

namespace NYT::NIO {

////////////////////////////////////////////////////////////////////////////////

class TDynamicIOEngine
    : public IDynamicIOEngine
{
public:
    TDynamicIOEngine(
        EIOEngineType defaultEngineType,
        NYTree::INodePtr defaultIOConfig,
        TFairShareHierarchicalSlotQueuePtr<std::string> fairShareQueue,
        IHugePageManagerPtr hugePageManager,
        TString locationId,
        NProfiling::TProfiler profiler,
        NLogging::TLogger logger)
        : LocationId_(std::move(locationId))
        , FairShareQueue_(std::move(fairShareQueue))
        , HugePageManager_(std::move(hugePageManager))
        , Profiler_(std::move(profiler))
        , Logger(std::move(logger))
    {
        SetType(defaultEngineType, defaultIOConfig);
        YT_LOG_INFO("Dynamic IO engine initialized (Type: %v)",
            defaultEngineType);

        for (auto engineType : GetSupportedIOEngineTypes()) {
            Profiler_
                .WithRequiredTag("engine_type", FormatEnum(engineType))
                .AddFuncGauge("/engine_enabled", MakeStrong(this), [this, engineType] {
                    return CurrentType_.load(std::memory_order::relaxed) == engineType ? 1.0 : 0.0;
                });
        }
    }

    TFuture<TReadResponse> Read(
        std::vector<TReadRequest> requests,
        EWorkloadCategory category,
        TRefCountedTypeCookie tagCookie,
        TIOSessionId sessionId,
        bool useDedicatedAllocations) override
    {
        return GetCurrentEngine()->Read(std::move(requests), category, tagCookie, sessionId, useDedicatedAllocations);
    }

    TFuture<TWriteResponse> Write(
        TWriteRequest request,
        EWorkloadCategory category,
        TIOSessionId sessionId) override
    {
        return GetCurrentEngine()->Write(std::move(request), category, sessionId);
    }

    TFuture<TFlushFileResponse> FlushFile(
        TFlushFileRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->FlushFile(std::move(request), category);
    }

    TFuture<TFlushFileRangeResponse> FlushFileRange(
        TFlushFileRangeRequest request,
        EWorkloadCategory category,
        TIOSessionId sessionId) override
    {
        return GetCurrentEngine()->FlushFileRange(std::move(request), category, sessionId);
    }

    TFuture<TFlushDirectoryResponse> FlushDirectory(
        TFlushDirectoryRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->FlushDirectory(std::move(request), category);
    }

    TFuture<TIOEngineHandlePtr> Open(
        TOpenRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->Open(std::move(request), category);
    }

    TFuture<TCloseResponse> Close(
        TCloseRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->Close(std::move(request), category);
    }

    TFuture<void> Allocate(
        TAllocateRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->Allocate(std::move(request), category);
    }

    TFuture<void> Lock(
        TLockRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->Lock(std::move(request), category);
    }

    TFuture<void> Resize(
        TResizeRequest request,
        EWorkloadCategory category) override
    {
        return GetCurrentEngine()->Resize(std::move(request), category);
    }

    bool IsSick() const override
    {
        return GetCurrentEngine()->IsSick();
    }

    void SetType(
        EIOEngineType type,
        const NYTree::INodePtr& ioConfig) override
    {
        auto guard = Guard(Lock_);

        auto& entry = TypeToEntry_[type];
        if (entry.Initialized.load()) {
            try {
                entry.Engine->Reconfigure(ioConfig);
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Error reconfiguring %Qlv IO engine",
                    type)
                    << ex;
            }
        } else {
            try {
                entry.Engine = CreateIOEngine(
                    type,
                    ioConfig,
                    LocationId_,
                    Profiler_,
                    Logger,
                    FairShareQueue_,
                    HugePageManager_);
                entry.Initialized.store(true);
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Error creating %Qlv IO engine",
                    type)
                    << ex;
            }
        }

        CurrentType_.store(type);

        YT_LOG_INFO("Dynamic IO engine reconfigured (Type: %v)",
            type);
    }

    void Reconfigure(const NYTree::INodePtr& dynamicIOConfig) override
    {
        GetCurrentEngine()->Reconfigure(dynamicIOConfig);
    }

    const IInvokerPtr& GetAuxPoolInvoker() override
    {
        return GetCurrentEngine()->GetAuxPoolInvoker();
    }

    i64 GetTotalReadBytes() const override
    {
        i64 total = 0;
        ForAllEngines([&] (const IIOEnginePtr& engine) {
            total += engine->GetTotalReadBytes();
        });
        return total;
    }

    i64 GetTotalWrittenBytes() const override
    {
        i64 total = 0;
        ForAllEngines([&] (const IIOEnginePtr& engine) {
            total += engine->GetTotalWrittenBytes();
        });
        return total;
    }

    EDirectIOPolicy UseDirectIOForReads() const override
    {
        return GetCurrentEngine()->UseDirectIOForReads();
    }

    bool IsInFlightReadRequestLimitExceeded() const override
    {
        return GetCurrentEngine()->IsInFlightReadRequestLimitExceeded();
    }

    bool IsInFlightWriteRequestLimitExceeded() const override
    {
        return GetCurrentEngine()->IsInFlightWriteRequestLimitExceeded();
    }

    i64 GetInFlightReadRequestCount() const override
    {
        return GetCurrentEngine()->GetInFlightReadRequestCount();
    }

    i64 GetReadRequestLimit() const override
    {
        return GetCurrentEngine()->GetReadRequestLimit();
    }

    i64 GetInFlightWriteRequestCount() const override
    {
        return GetCurrentEngine()->GetInFlightWriteRequestCount();
    }

    i64 GetWriteRequestLimit() const override
    {
        return GetCurrentEngine()->GetWriteRequestLimit();
    }

private:
    const TString LocationId_;
    const TFairShareHierarchicalSlotQueuePtr<std::string> FairShareQueue_ = nullptr;
    const IHugePageManagerPtr HugePageManager_ = nullptr;
    const NProfiling::TProfiler Profiler_;
    const NLogging::TLogger Logger;

    YT_DECLARE_SPIN_LOCK(mutable NThreading::TSpinLock, Lock_);

    struct TEngineEntry
    {
        std::atomic<bool> Initialized = false;
        IIOEnginePtr Engine;
    };

    std::atomic<EIOEngineType> CurrentType_;
    mutable TEnumIndexedArray<EIOEngineType, TEngineEntry> TypeToEntry_;

    const IIOEnginePtr& GetCurrentEngine() const
    {
        auto type = CurrentType_.load(std::memory_order::relaxed);
        return TypeToEntry_[type].Engine;
    }

    template <class TFn>
    void ForAllEngines(const TFn& cb) const
    {
        for (const auto& entry : TypeToEntry_) {
            if (entry.Initialized.load()) {
                cb(entry.Engine);
            }
        }
    }
};

DEFINE_REFCOUNTED_TYPE(TDynamicIOEngine)

////////////////////////////////////////////////////////////////////////////////

IDynamicIOEnginePtr CreateDynamicIOEngine(
    EIOEngineType defaultEngineType,
    NYTree::INodePtr ioConfig,
    TFairShareHierarchicalSlotQueuePtr<std::string> fairShareQueue,
    IHugePageManagerPtr hugePageManager,
    TString locationId,
    NProfiling::TProfiler profiler,
    NLogging::TLogger logger)
{
    return New<TDynamicIOEngine>(
        defaultEngineType,
        std::move(ioConfig),
        std::move(fairShareQueue),
        std::move(hugePageManager),
        std::move(locationId),
        std::move(profiler),
        std::move(logger));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NIO
