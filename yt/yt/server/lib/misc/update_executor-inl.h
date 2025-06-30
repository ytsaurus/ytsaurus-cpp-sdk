#ifndef UPDATE_EXECUTOR_INL_H_
#error "Direct inclusion of this file is not allowed, include update_executor.h"
// For the sake of sane code completion.
#include "update_executor.h"
#endif

namespace NYT::NServer {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TUpdateParameters>
TUpdateExecutor<TKey, TUpdateParameters>::TUpdateExecutor(
    IInvokerPtr invoker,
    TCallback<TCallback<TFuture<void>()>(const TKey&, TUpdateParameters*)> createUpdateAction,
    TCallback<bool(const TUpdateParameters*)> shouldRemoveUpdateAction,
    TCallback<void(const TError&)> onUpdateFailed,
    TDuration period,
    const NLogging::TLogger& logger)
    : CreateUpdateAction_(createUpdateAction)
    , ShouldRemoveUpdateAction_(shouldRemoveUpdateAction)
    , OnUpdateFailed_(onUpdateFailed)
    , Invoker_(std::move(invoker))
    , Logger(logger)
    , UpdateExecutor_(New<NConcurrency::TPeriodicExecutor>(
        Invoker_,
        BIND(&TUpdateExecutor<TKey, TUpdateParameters>::ExecuteUpdates, MakeWeak(this)),
        period))
{ }

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::Start()
{
    UpdateExecutor_->Start();
}

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::Stop()
{
    YT_UNUSED_FUTURE(UpdateExecutor_->Stop());
}

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::SetPeriod(TDuration period)
{
    UpdateExecutor_->SetPeriod(period);
}

template <class TKey, class TUpdateParameters>
TUpdateParameters* TUpdateExecutor<TKey, TUpdateParameters>::AddUpdate(const TKey& key, const TUpdateParameters& parameters)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto pair = Updates_.emplace(key, TUpdateRecord(key, parameters));
    YT_VERIFY(pair.second);
    YT_LOG_DEBUG("Item added to periodic updates (Key: %v)", key);
    return &pair.first->second.UpdateParameters;
}

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::RemoveUpdate(const TKey& key)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    YT_VERIFY(Updates_.erase(key) == 1);
    YT_LOG_DEBUG("Item removed from periodic updates (Key: %v)", key);
}

template <class TKey, class TUpdateParameters>
TUpdateParameters* TUpdateExecutor<TKey, TUpdateParameters>::FindUpdate(const TKey& key)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto* result = FindUpdateRecord(key);
    return result ? &result->UpdateParameters : nullptr;
}

template <class TKey, class TUpdateParameters>
TUpdateParameters* TUpdateExecutor<TKey, TUpdateParameters>::GetUpdate(const TKey& key)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto* result = FindUpdate(key);
    YT_VERIFY(result);
    return result;
}

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::Clear()
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    Updates_.clear();
}

template <class TKey, class TUpdateParameters>
void TUpdateExecutor<TKey, TUpdateParameters>::ExecuteUpdates()
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    YT_LOG_INFO("Updating items (Count: %v)", Updates_.size());

    std::vector<TKey> updatesToRemove;
    std::vector<TFuture<void>> asyncResults;
    std::vector<TKey> requestKeys;
    for (auto& [key, updateRecord] : Updates_) {
        if (ShouldRemoveUpdateAction_(&updateRecord.UpdateParameters)) {
            updatesToRemove.push_back(key);
        } else {
            YT_LOG_DEBUG("Updating item (Key: %v)", key);
            requestKeys.push_back(key);
            asyncResults.push_back(DoExecuteUpdate(&updateRecord));
        }
    }

    // Cleanup.
    for (auto key : updatesToRemove) {
        RemoveUpdate(key);
    }

    auto result = NConcurrency::WaitFor(AllSet(asyncResults));
    YT_VERIFY(result.IsOK());
    if (!result.IsOK()) {
        OnUpdateFailed_(result);
        return;
    }

    YT_LOG_INFO("Update completed");
}

template <class TKey, class TUpdateParameters>
TFuture<void> TUpdateExecutor<TKey, TUpdateParameters>::ExecuteUpdate(const TKey& key)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto* updateRecord = FindUpdateRecord(key);
    if (!updateRecord) {
        return VoidFuture;
    }
    return DoExecuteUpdate(updateRecord);
}

template <class TKey, class TUpdateParameters>
typename TUpdateExecutor<TKey, TUpdateParameters>::TUpdateRecord* TUpdateExecutor<TKey, TUpdateParameters>::FindUpdateRecord(const TKey& key)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto it = Updates_.find(key);
    return it == Updates_.end() ? nullptr : &it->second;
}

template <class TKey, class TUpdateParameters>
TCallback<TFuture<void>()> TUpdateExecutor<TKey, TUpdateParameters>::CreateUpdateAction(const TKey& key, TUpdateParameters* updateParameters)
{
    auto updateAction = CreateUpdateAction_(key, updateParameters);
    if (!updateAction) {
        return {};
    }

    return BIND([key, this, updateAction = std::move(updateAction), this_ = MakeStrong(this)] () {
            return updateAction().Apply(
                BIND([=, this, this_ = MakeStrong(this)] (const TError& error) {
                    if (!error.IsOK()) {
                        OnUpdateFailed_(TError("Update of item failed (Key: %v)", key) << error);
                    }
                })
                .AsyncVia(Invoker_));
        })
        .AsyncVia(Invoker_);
}

template <class TKey, class TUpdateParameters>
TFuture<void> TUpdateExecutor<TKey, TUpdateParameters>::DoExecuteUpdate(TUpdateRecord* updateRecord)
{
    YT_ASSERT_THREAD_AFFINITY(UpdateThread);

    auto callback = CreateUpdateAction(updateRecord->Key, &updateRecord->UpdateParameters);
    if (!callback) {
        return updateRecord->LastUpdateFuture;
    }
    updateRecord->LastUpdateFuture = updateRecord->LastUpdateFuture.Apply(callback);
    return updateRecord->LastUpdateFuture;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NServer
