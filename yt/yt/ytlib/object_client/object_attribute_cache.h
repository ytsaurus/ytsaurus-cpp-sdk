#pragma once

#include "public.h"

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/client/api/client.h>

#include <yt/yt/client/misc/public.h>

#include <yt/yt/core/misc/async_expiring_cache.h>

#include <util/generic/set.h>

namespace NYT::NObjectClient {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue>
class TObjectAttributeCacheBase
    : public TAsyncExpiringCache<TKey, TValue>
{
public:
    TObjectAttributeCacheBase(
        TObjectAttributeCacheConfigPtr config,
        TWeakPtr<NApi::NNative::IConnection> connection,
        IInvokerPtr invoker,
        const NLogging::TLogger& logger = {},
        NProfiling::TProfiler profiler = {});

    //! Same as GetMany, but does not affect the internal cache state and allows substituting the client.
    TFuture<std::vector<TErrorOr<TValue>>> GetFromClient(const std::vector<TKey>& keys, const NApi::NNative::IClientPtr& client) noexcept;

protected:
    //! Should return the path on which to fetch attributes for the given key.
    virtual NYPath::TYPath GetPath(const TKey& key) const = 0;
    //! Should construct cache value from the given set of fetched attributes.
    //! Can throw if value cannot be constructed, this error will be propagated to the caller.
    virtual TValue ParseValue(const NYTree::IAttributeDictionaryPtr& attributes) const = 0;

    //! Should return a list of attribute names to fetch.
    virtual const std::vector<std::string>& GetAttributeNames() const = 0;

    //! Should return a list of minimum revisions to fetch, or an empty list.
    virtual std::vector<NHydra::TRevision> GetRefreshRevisions(const std::vector<TKey>& keys) const = 0;

private:
    const TObjectAttributeCacheConfigPtr Config_;
    const NLogging::TLogger Logger;

    TWeakPtr<NApi::NNative::IConnection> Connection_;
    IInvokerPtr Invoker_;

    //! TAsyncExpiringCache<TKey, TValue> implementation.
    TFuture<TValue> DoGet(
        const TKey& key,
        bool isPeriodicUpdate) noexcept override;
    TFuture<std::vector<TErrorOr<TValue>>> DoGetMany(
        const std::vector<TKey>& keys,
        bool isPeriodicUpdate) noexcept override;
};

////////////////////////////////////////////////////////////////////////////////

//! This base derives the list of attributes from the list of registerd fields in a YsonStruct value,
//! and uses classic ConvertTo to build construct the value from fetched attributes.
//! For now, the provided YsonStruct must provide defaults for every field, otherwise we cannot use
//! the value type to infer attribute names.
template <class TKey, class TValue>
    requires std::derived_from<typename TValue::TUnderlying, NYTree::TYsonStruct>
class TObjectAttributeAsYsonStructCacheBase
    : public TObjectAttributeCacheBase<TKey, TValue>
{
public:
    TObjectAttributeAsYsonStructCacheBase(
        TObjectAttributeCacheConfigPtr config,
        TWeakPtr<NApi::NNative::IConnection> connection,
        IInvokerPtr invoker,
        const NLogging::TLogger& logger,
        NProfiling::TProfiler profiler);

private:
    std::vector<std::string> AttributeNames_;

    //! TObjectAttributeCacheBase<TKey, TValue> implementation.
    TValue ParseValue(const NYTree::IAttributeDictionaryPtr& attributes) const override;
    const std::vector<std::string>& GetAttributeNames() const override;

    std::vector<NHydra::TRevision> GetRefreshRevisions(const std::vector<TKey>& keys) const override;
};

////////////////////////////////////////////////////////////////////////////////

class TObjectAttributeCache
    : public TObjectAttributeCacheBase<NYPath::TYPath, NYTree::IAttributeDictionaryPtr>
{
public:
    TObjectAttributeCache(
        TObjectAttributeCacheConfigPtr config,
        std::vector<std::string> attributeNames,
        TWeakPtr<NApi::NNative::IConnection> connection,
        IInvokerPtr invoker,
        const NLogging::TLogger& logger = {},
        NProfiling::TProfiler profiler = {});

    void InvalidateActiveAndSetRefreshRevision(const NYPath::TYPath& key, NHydra::TRevision revision);

private:
    const std::vector<std::string> AttributeNames_;

    class TRevisionStorage
    {
    public:
        explicit TRevisionStorage(ui64 maxPathsSize);

        void Add(const NYPath::TYPath& path, NHydra::TRevision revision);

        NHydra::TRevision Get(const NYPath::TYPath& path) const;

        NHydra::TRevision GetDefault() const;

    private:
        const ui64 MaxPathsSize_;

        THashMap<NYPath::TYPath, NHydra::TRevision> RevisionMap_;
        TSet<std::pair<NHydra::TRevision, NYPath::TYPath>> Paths_;

        NHydra::TRevision DefaultRevision_ = NHydra::NullRevision;

        void Remove(const NYPath::TYPath& path, bool updateDefault = false);
    };

    TRevisionStorage RefreshRevisionStorage_;

    NYPath::TYPath GetPath(const NYPath::TYPath& key) const override;
    NYTree::IAttributeDictionaryPtr ParseValue(const NYTree::IAttributeDictionaryPtr& attributes) const override;
    const std::vector<std::string>& GetAttributeNames() const override;

    std::vector<NHydra::TRevision> GetRefreshRevisions(const std::vector<NYPath::TYPath>& keys) const override;
};

DEFINE_REFCOUNTED_TYPE(TObjectAttributeCache)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectClient

#define OBJECT_ATTRIBUTE_CACHE_INL_H_
#include "object_attribute_cache-inl.h"
#undef OBJECT_ATTRIBUTE_CACHE_INL_H_
