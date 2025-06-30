#ifndef ASYNC_BATCHER_INL_H_
#error "Direct inclusion of this file is not allowed, include async_batcher.h"
// For the sake of sane code completion.
#include "async_batcher.h"
#endif
#undef ASYNC_BATCHER_INL_H_

#include "delayed_executor.h"
#include "thread_affinity.h"
#include "public.h"
#include "scheduler_api.h"

#include <yt/yt/core/tracing/trace_context.h>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

template <class T>
TAsyncBatcher<T>::TAsyncBatcher(TCallback<TFuture<T>()> provider, TDuration batchingDelay)
    : Provider_(std::move(provider))
    , BatchingDelay_(batchingDelay)
{ }

template <class T>
TFuture<T> TAsyncBatcher<T>::Run()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = Guard(Lock_);
    auto promise = PendingPromise_;
    if (!promise) {
        promise = PendingPromise_ = NewPromise<void>();
        if (BatchingDelay_ == TDuration::Zero()) {
            DeadlineReached_ = true;
            if (!ActivePromise_) {
                NTracing::TNullTraceContextGuard traceContextGuard;
                DoRun(guard);
            }
        } else {
            TDelayedExecutor::Submit(
                BIND_NO_PROPAGATE(&TAsyncBatcher::OnDeadlineReached, MakeWeak(this)),
                BatchingDelay_);
        }
    }
    return promise
        .ToFuture()
        .ToUncancelable();
}

template <class T>
void TAsyncBatcher<T>::Cancel(const TError& error)
{
    auto lockGuard = Guard(Lock_);
    std::array<TPromise<T>, 2> promises{
        std::move(PendingPromise_),
        std::move(ActivePromise_)
    };
    lockGuard.Release();

    NTracing::TNullTraceContextGuard traceContextGuard;
    for (const auto& promise : promises) {
        if (promise) {
            promise.TrySet(error);
        }
    }
}

template <class T>
void TAsyncBatcher<T>::OnDeadlineReached()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = Guard(Lock_);

    DeadlineReached_ = true;

    if (!ActivePromise_) {
        DoRun(guard);
    }
}

template <class T>
void TAsyncBatcher<T>::DoRun(TGuard<NThreading::TSpinLock>& guard)
{
    YT_ASSERT_SPINLOCK_AFFINITY(Lock_);

    DeadlineReached_ = false;

    YT_VERIFY(!ActivePromise_);
    swap(ActivePromise_, PendingPromise_);

    guard.Release();

    YT_VERIFY(GetCurrentFiberId() != InvalidFiberId);

    Provider_
        .Run()
        .Subscribe(BIND_NO_PROPAGATE(&TAsyncBatcher::OnResult, MakeWeak(this)));
}

template <class T>
void TAsyncBatcher<T>::OnResult(const TErrorOr<T>& result)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = Guard(Lock_);
    auto activePromise = std::move(ActivePromise_);

    if (DeadlineReached_) {
        DoRun(guard);
    }

    guard.Release();

    if (activePromise) {
        activePromise.TrySet(result);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
