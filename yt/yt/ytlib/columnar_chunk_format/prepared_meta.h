#pragma once

#include "public.h"

#include <yt/yt/client/table_client/row_base.h>

#include <yt/yt/core/misc/public.h>

namespace NYT::NColumnarChunkFormat {

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE bool IsDirect(int type);

Y_FORCE_INLINE bool IsDense(int type);

struct IBlockDataProvider
{
    virtual const char* GetBlock(ui32 blockIndex) = 0;

    virtual ~IBlockDataProvider() = default;
};

template <class T>
TRef MetaToRef(const T& meta)
{
    return {reinterpret_cast<const char*>(&meta), sizeof(meta)};
}

struct TMetaBase
{
    ui64 DataOffset;
    // RowCount can be evaluated from ChunkRowCount of adjacent segments.
    ui32 RowCount;
    ui32 ChunkRowCount;

    void InitFromProto(const NProto::TSegmentMeta& meta);
};

struct TTimestampMeta
    : public TMetaBase
{
    ui64 BaseTimestamp;
    ui32 ExpectedDeletesPerRow;
    ui32 ExpectedWritesPerRow;

    ui32 TimestampsDictSize = 0;
    ui32 WriteTimestampSize = 0;
    ui32 DeleteTimestampSize = 0;
    ui32 WriteOffsetDiffsSize = 0;
    ui32 DeleteOffsetDiffsSize = 0;

    ui8 TimestampsDictWidth = 0;
    ui8 WriteTimestampWidth = 0;
    ui8 DeleteTimestampWidth = 0;
    ui8 WriteOffsetDiffsWidth = 0;
    ui8 DeleteOffsetDiffsWidth = 0;

    void InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

struct TIntegerMeta
{
    ui64 BaseValue;

    ui32 ValuesSize = 0;
    ui32 IdsSize = 0;
    ui8 ValuesWidth = 0;
    ui8 IdsWidth = 0;

    bool Direct;

    const ui64* InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

struct TBlobMeta
{
    ui32 ExpectedLength;

    ui32 IdsSize = 0;
    ui32 OffsetsSize = 0;
    ui8 IdsWidth = 0;
    ui8 OffsetsWidth = 0;

    bool Direct;

    void InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

struct TEmptyMeta
{ };

template <EValueType Type>
struct TDataMeta;

template <>
struct TDataMeta<EValueType::Int64>
    : public TIntegerMeta
{ };

template <>
struct TDataMeta<EValueType::Uint64>
    : public TIntegerMeta
{ };

template <>
struct TDataMeta<EValueType::Boolean>
    : public TEmptyMeta
{
    static const ui64* InitFromProto(const NProto::TSegmentMeta& /*meta*/, const ui64* ptr);
};

template <>
struct TDataMeta<EValueType::Double>
    : public TEmptyMeta
{
    static const ui64* InitFromProto(const NProto::TSegmentMeta& /*meta*/, const ui64* ptr);
};

template <>
struct TDataMeta<EValueType::String>
    : public TBlobMeta
{ };

template <>
struct TDataMeta<EValueType::Any>
    : public TBlobMeta
{ };

template <>
struct TDataMeta<EValueType::Composite>
    : public TBlobMeta
{ };

struct TMultiValueIndexMeta
    : public TMetaBase
{
    ui32 ExpectedPerRow;

    // Offsets are perRowDiff for dense or rowIndexes for sparse
    ui32 OffsetsSize = 0;
    ui32 WriteTimestampIdsSize = 0;
    ui8 OffsetsWidth = 0;
    ui8 WriteTimestampIdsWidth = 0;

    const ui64* InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr, bool aggregate);

    Y_FORCE_INLINE bool IsDense() const;
};

template <EValueType Type>
struct TValueMeta
    : public TMultiValueIndexMeta
    , public TDataMeta<Type>
{
    void InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

template <EValueType Type>
struct TAggregateValueMeta
    : public TValueMeta<Type>
{
    void InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

struct TKeyIndexMeta
    : public TMetaBase
{
    ui32 RowIndexesSize = 0;
    ui8 RowIndexesWidth = 0;

    bool Dense;

    const ui64* InitFromProto(const NProto::TSegmentMeta& meta, EValueType type, const ui64* ptr);
};

template <EValueType Type>
struct TKeyMeta
    : public TKeyIndexMeta
    , public TDataMeta<Type>
{
    void InitFromProto(const NProto::TSegmentMeta& meta, const ui64* ptr);
};

struct TPreparedChunkMeta final
{
    // Do not want to use term ColumnGroup here because it can be сonfused with Column.
    struct TGroupInfo
    {
        TGroupInfo() = default;
        TGroupInfo(const TGroupInfo&) = delete;
        TGroupInfo(TGroupInfo&&) = default;

        std::vector<ui32> BlockIds;
        std::vector<ui32> BlockChunkRowCounts;
        // Per block segment metas for each column in group.
        // Contains mapping from column index in group to offsets and serialized segment metas.

        // Prepared segment metas from protobuf representation.
        std::vector<TSharedRef> MergedMetas;
        // Offsets of segment meta stored in blocks.
        std::vector<ui32> SegmentMetaOffsets;
    };

    struct TColumnInfo
    {
        ui16 GroupId;
        ui16 IndexInGroup;
    };

    std::vector<TGroupInfo> GroupInfos;
    std::vector<TColumnInfo> ColumnInfos;

    size_t Size = 0;
    bool FullNewMeta = false;

    static TIntrusivePtr<TPreparedChunkMeta> FromProtoSegmentMetas(
        const NTableClient::TTableSchemaPtr& chunkSchema,
        const NTableClient::TRefCountedColumnMetaPtr& columnMetas,
        const NTableClient::TRefCountedDataBlockMetaPtr& blockMeta,
        IBlockDataProvider* blockProvider = nullptr);

    static TIntrusivePtr<TPreparedChunkMeta> FromSegmentMetasStoredInBlocks(
        const NTableClient::TRefCountedColumnGroupInfosExtPtr& columnGroupInfos,
        const NTableClient::TRefCountedDataBlockMetaPtr& blockMeta);

    static void VerifyEquality(
        const TPreparedChunkMeta& fromProtoMeta,
        const TPreparedChunkMeta& inBlocksMeta,
        const NTableClient::TRefCountedDataBlockMetaPtr& blockMeta);
};

////////////////////////////////////////////////////////////////////////////////

template <class TRawMeta>
void VerifyRawSegmentMeta(const NProto::TSegmentMeta& protoMeta, TRange<TSharedRef> segmentData, const TRawMeta& rawMeta);

template <EValueType Type>
void VerifyRawVersionedSegmentMeta(
    const NProto::TSegmentMeta& protoMeta,
    TRange<TSharedRef> segmentData,
    const TValueMeta<Type>& rawMeta,
    bool aggregate);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NColumnarChunkFormat

#define PREPARED_META_INL_H_
#include "prepared_meta-inl.h"
#undef PREPARED_META_INL_H_
