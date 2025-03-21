#pragma once

#include <yt/yt/core/misc/public.h>
#include <yt/yt/core/misc/configurable_singleton_decl.h>

#include <library/cpp/yt/misc/enum.h>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TActionQueue)
DECLARE_REFCOUNTED_STRUCT(IThreadPool)

DECLARE_REFCOUNTED_STRUCT(ISuspendableActionQueue)

DECLARE_REFCOUNTED_CLASS(TPeriodicExecutor)
DECLARE_REFCOUNTED_CLASS(TRetryingPeriodicExecutor)
DECLARE_REFCOUNTED_CLASS(TScheduledExecutor)
DECLARE_REFCOUNTED_CLASS(TInvokerAlarm)

DECLARE_REFCOUNTED_CLASS(TAsyncSemaphore)
DECLARE_REFCOUNTED_CLASS(TProfiledAsyncSemaphore)

DECLARE_REFCOUNTED_STRUCT(IFairShareActionQueue)

DECLARE_REFCOUNTED_STRUCT(IQuantizedExecutor)

DECLARE_REFCOUNTED_CLASS(TAsyncLooper);

namespace NDetail {

DECLARE_REFCOUNTED_STRUCT(TDelayedExecutorEntry)

} // namespace NDetail

using TDelayedExecutorCookie = NDetail::TDelayedExecutorEntryPtr;

DECLARE_REFCOUNTED_STRUCT(TThroughputThrottlerConfig)
DECLARE_REFCOUNTED_STRUCT(TRelativeThroughputThrottlerConfig)
DECLARE_REFCOUNTED_STRUCT(TPrefetchingThrottlerConfig)
DECLARE_REFCOUNTED_STRUCT(IThroughputThrottler)
DECLARE_REFCOUNTED_STRUCT(IReconfigurableThroughputThrottler)
DECLARE_REFCOUNTED_STRUCT(ITestableReconfigurableThroughputThrottler)

DECLARE_REFCOUNTED_STRUCT(IAsyncInputStream)
DECLARE_REFCOUNTED_STRUCT(IAsyncOutputStream)

DECLARE_REFCOUNTED_STRUCT(IFlushableAsyncOutputStream)

DECLARE_REFCOUNTED_STRUCT(IAsyncZeroCopyInputStream)
DECLARE_REFCOUNTED_STRUCT(IAsyncZeroCopyOutputStream)

DECLARE_REFCOUNTED_STRUCT(IFairShareThreadPool)

DECLARE_REFCOUNTED_CLASS(TAsyncStreamPipe)
DECLARE_REFCOUNTED_CLASS(TBoundedAsyncStreamPipe)

DEFINE_ENUM(EWaitForStrategy,
    (WaitFor)
    (Get)
);

class TAsyncSemaphore;

DEFINE_ENUM(EExecutionStackKind,
    (Small) // 256 Kb (default)
    (Large) //   8 Mb
);

class TExecutionStack;

template <class TSignature>
class TCoroutine;

template <class T>
class TNonblockingQueue;

template <typename EQueue>
struct IEnumIndexedFairShareActionQueue;

template <typename EQueue>
using IEnumIndexedFairShareActionQueuePtr = TIntrusivePtr<IEnumIndexedFairShareActionQueue<EQueue>>;

DECLARE_REFCOUNTED_STRUCT(TLeaseEntry)
using TLease = TLeaseEntryPtr;

DECLARE_REFCOUNTED_STRUCT(IPollable)
DECLARE_REFCOUNTED_STRUCT(IPoller)
DECLARE_REFCOUNTED_STRUCT(IThreadPoolPoller)

DECLARE_REFCOUNTED_CLASS(TThread)

constexpr int DefaultMaxIdleFibers = 5'000;
constexpr int DefaultFiberStackPoolSize = 1'000;

using TFiberId = size_t;
constexpr size_t InvalidFiberId = 0;

DEFINE_ENUM(EFiberState,
    (Created)
    (Running)
    (Introspecting)
    (Waiting)
    (Idle)
    (Finished)
);

using TFairShareThreadPoolTag = std::string;

DECLARE_REFCOUNTED_STRUCT(IPoolWeightProvider)

DECLARE_REFCOUNTED_STRUCT(ITwoLevelFairShareThreadPool)

class TFiber;

DECLARE_REFCOUNTED_STRUCT(TFiberManagerConfig)
DECLARE_REFCOUNTED_STRUCT(TFiberManagerDynamicConfig)

DECLARE_REFCOUNTED_STRUCT(TFairThrottlerConfig)
DECLARE_REFCOUNTED_STRUCT(TFairThrottlerBucketConfig)

DECLARE_REFCOUNTED_STRUCT(IThrottlerIpc)
DECLARE_REFCOUNTED_STRUCT(IIpcBucket)

DECLARE_REFCOUNTED_CLASS(TFairThrottler)
DECLARE_REFCOUNTED_CLASS(TBucketThrottler)

DECLARE_REFCOUNTED_STRUCT(ICallbackProvider)

class TPropagatingStorage;

YT_DECLARE_RECONFIGURABLE_SINGLETON(TFiberManagerConfig, TFiberManagerDynamicConfig);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
