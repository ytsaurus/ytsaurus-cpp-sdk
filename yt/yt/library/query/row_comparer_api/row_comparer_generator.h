#pragma once

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/client/table_client/public.h>
#include <yt/yt/client/table_client/schema.h>
#include <yt/yt/client/table_client/unversioned_row.h>

#include <yt/yt/client/tablet_client/dynamic_value.h>

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NQueryClient {

using NTableClient::EValueType;
using NTableClient::TDynamicTableKeyMask;
using NTableClient::TUnversionedValue;
using NTableClient::TUnversionedRow;
using NTabletClient::TDynamicValueData;

////////////////////////////////////////////////////////////////////////////////

using TDDComparerSignature = int(TDynamicTableKeyMask, const TDynamicValueData*, TDynamicTableKeyMask, const TDynamicValueData*);
using TDUComparerSignature = int(TDynamicTableKeyMask, const TDynamicValueData*, const TUnversionedValue*, int);
using TUUComparerSignature = NYT::NTableClient::TUUComparerSignature;

struct TCGKeyComparers
{
    TCallback<TDDComparerSignature> DDComparer;
    TCallback<TDUComparerSignature> DUComparer;
    TCallback<TUUComparerSignature> UUComparer;
};

////////////////////////////////////////////////////////////////////////////////

TCGKeyComparers GenerateComparers(TRange<EValueType> keyColumnTypes);

////////////////////////////////////////////////////////////////////////////////

TCallback<TUUComparerSignature> GenerateStaticTableKeyComparer(TRange<EValueType> keyColumnTypes);

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(IRowComparerProvider)

struct IRowComparerProvider
    : public virtual TRefCounted
{
    virtual TCGKeyComparers Get(NTableClient::TKeyColumnTypes keyColumnTypes) = 0;
};

DEFINE_REFCOUNTED_TYPE(IRowComparerProvider)

IRowComparerProviderPtr CreateRowComparerProvider(TSlruCacheConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
