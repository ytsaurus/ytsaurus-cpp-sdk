#include "integer_column_writer.h"
#include "helpers.h"
#include "data_block_writer.h"
#include "column_writer_detail.h"

#include <yt/yt_proto/yt/client/table_chunk_format/proto/column_meta.pb.h>

#include <yt/yt/core/misc/bitmap.h>
#include <yt/yt/core/misc/bit_packed_unsigned_vector.h>

#include <library/cpp/yt/coding/zig_zag.h>

namespace NYT::NTableChunkFormat {

using namespace NTableClient;
using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

namespace {

ui64 EncodeValue(i64 value)
{
    return ZigZagEncode64(value);
}

ui64 EncodeValue(ui64 value)
{
    return value;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class TIntegerColumnWriterBase
{
protected:
    std::vector<ui64> Values_;

    ui64 MaxValue_;
    ui64 MinValue_;
    THashMap<ui64, int> DistinctValues_;

    void Reset()
    {
        Values_.clear();

        MaxValue_ = 0;
        MinValue_ = std::numeric_limits<ui64>::max();

        DistinctValues_.clear();
    }

    void UpdateStatistics(ui64 value)
    {
        MaxValue_ = std::max(MaxValue_, value);
        MinValue_ = std::min(MinValue_, value);
        DistinctValues_.emplace(value, DistinctValues_.size() + 1);
    }

    void DumpDirectValues(TSegmentInfo* segmentInfo, TBitmapOutput& nullBitmap, NColumnarChunkFormat::TIntegerMeta* rawMeta)
    {
        for (i64 index = 0; index < std::ssize(Values_); ++index) {
            if (!nullBitmap[index]) {
                Values_[index] -= MinValue_;
            }
        }

        rawMeta->BaseValue = MinValue_;
        rawMeta->Direct = true;

        // 1. Direct values.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(Values_), MaxValue_ - MinValue_, &rawMeta->ValuesSize, &rawMeta->ValuesWidth));

        // 2. Null bitmap.
        segmentInfo->Data.push_back(nullBitmap.Flush<TSegmentWriterTag>());
    }

    void DumpDictionaryValues(TSegmentInfo* segmentInfo, TBitmapOutput& nullBitmap, NColumnarChunkFormat::TIntegerMeta* rawMeta)
    {
        std::vector<ui64> dictionary;
        dictionary.reserve(DistinctValues_.size());

        for (i64 index = 0; index < std::ssize(Values_); ++index) {
            if (nullBitmap[index]) {
                YT_ASSERT(Values_[index] == 0);
            } else {
                auto dictionaryIndex = GetOrCrash(DistinctValues_, Values_[index]);
                YT_ASSERT(dictionaryIndex <= std::ssize(dictionary) + 1);

                if (dictionaryIndex > std::ssize(dictionary)) {
                    dictionary.push_back(Values_[index] - MinValue_);
                }

                Values_[index] = dictionaryIndex;
            }
        }

        rawMeta->BaseValue = MinValue_;
        rawMeta->Direct = false;

        // 1. Dictionary - compressed vector of values.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(dictionary), MaxValue_ - MinValue_, &rawMeta->ValuesSize, &rawMeta->ValuesWidth));

        // 2. Compressed vector of value ids.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(Values_), dictionary.size() + 1, &rawMeta->IdsSize, &rawMeta->IdsWidth));
    }
};

////////////////////////////////////////////////////////////////////////////////

// TValue - i64 or ui64.
template <class TValue>
class TVersionedIntegerColumnWriter
    : public TVersionedColumnWriterBase
    , private TIntegerColumnWriterBase
{
public:
    TVersionedIntegerColumnWriter(
        int columnId,
        const TColumnSchema& columnSchema,
        TDataBlockWriter* blockWriter,
        int maxValueCount)
        : TVersionedColumnWriterBase(
            columnId,
            columnSchema,
            blockWriter)
        , MaxValueCount_(maxValueCount)
    {
        Reset();
    }

    void WriteVersionedValues(TRange<TVersionedRow> rows) override
    {
        AddValues(
            rows,
            [&] (const TVersionedValue& value) {
                ui64 data = 0;
                if (value.Type != EValueType::Null) {
                    data = EncodeValue(GetValue<TValue>(value));
                }
                Values_.push_back(data);
                return std::ssize(Values_) >= MaxValueCount_;
            });
    }

    i32 GetCurrentSegmentSize() const override
    {
        if (TVersionedColumnWriterBase::ValuesPerRow_.empty()) {
            return 0;
        } else {
            return std::min(GetDirectSize(), GetDictionarySize()) +
                TVersionedColumnWriterBase::GetCurrentSegmentSize();
        }
    }

    void FinishCurrentSegment() override
    {
        if (!TVersionedColumnWriterBase::ValuesPerRow_.empty()) {
            DumpSegment();
            Reset();
        }
    }

private:
    const int MaxValueCount_;

    void Reset()
    {
        TVersionedColumnWriterBase::Reset();
        TIntegerColumnWriterBase::Reset();
    }

    size_t GetDictionarySize() const
    {
        return
            CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, DistinctValues_.size()) +
            CompressedUnsignedVectorSizeInBytes(DistinctValues_.size() + 1, Values_.size());
    }

    size_t GetDirectSize() const
    {
        return
            CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, Values_.size()) +
            NullBitmap_.GetByteSize();
    }

    void DumpSegment()
    {
        for (i64 index = 0; index < std::ssize(Values_); ++index) {
            if (NullBitmap_[index]) {
                YT_ASSERT(Values_[index] == 0);
            } else {
                UpdateStatistics(Values_[index]);
            }
        }

        TSegmentInfo segmentInfo;
        segmentInfo.SegmentMeta.set_version(0);

        auto* meta = segmentInfo.SegmentMeta.MutableExtension(NProto::TIntegerSegmentMeta::integer_segment_meta);
        meta->set_min_value(MinValue_);

        constexpr EValueType ValueType = std::is_same_v<ui64, TValue> ? EValueType::Uint64 : EValueType::Int64;

        NColumnarChunkFormat::TValueMeta<ValueType> rawMeta;
        memset(&rawMeta, 0, sizeof(rawMeta));
        rawMeta.DataOffset = TColumnWriterBase::GetOffset();
        rawMeta.ChunkRowCount = RowCount_;

        DumpVersionedData(&segmentInfo, &rawMeta);

        ui64 dictionarySize = GetDictionarySize();
        ui64 directSize = GetDirectSize();

        if (dictionarySize < directSize) {
            DumpDictionaryValues(&segmentInfo, NullBitmap_, &rawMeta);

            segmentInfo.SegmentMeta.set_type(static_cast<int>(segmentInfo.Dense
                ? EVersionedIntegerSegmentType::DictionaryDense
                : EVersionedIntegerSegmentType::DictionarySparse));

        } else {
            DumpDirectValues(&segmentInfo, NullBitmap_, &rawMeta);

            segmentInfo.SegmentMeta.set_type(static_cast<int>(segmentInfo.Dense
                ? EVersionedIntegerSegmentType::DirectDense
                : EVersionedIntegerSegmentType::DirectSparse));
        }

        TColumnWriterBase::DumpSegment(&segmentInfo, TSharedRef::MakeCopy<TSegmentWriterTag>(MetaToRef(rawMeta)));

        if (BlockWriter_->GetEnableSegmentMetaInBlocks()) {
            VerifyRawVersionedSegmentMeta(segmentInfo.SegmentMeta, segmentInfo.Data, rawMeta, Aggregate_);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IValueColumnWriter> CreateVersionedInt64ColumnWriter(
    int columnId,
    const TColumnSchema& columnSchema,
    TDataBlockWriter* dataBlockWriter,
    int maxValueCount)
{
    return std::make_unique<TVersionedIntegerColumnWriter<i64>>(
        columnId,
        columnSchema,
        dataBlockWriter,
        maxValueCount);
}

std::unique_ptr<IValueColumnWriter> CreateVersionedUint64ColumnWriter(
    int columnId,
    const TColumnSchema& columnSchema,
    TDataBlockWriter* dataBlockWriter,
    int maxValueCount)
{
    return std::make_unique<TVersionedIntegerColumnWriter<ui64>>(
        columnId,
        columnSchema,
        dataBlockWriter,
        maxValueCount);
}

////////////////////////////////////////////////////////////////////////////////

// TValue - i64 or ui64.
template <class TValue>
class TUnversionedIntegerColumnWriter
    : public TColumnWriterBase
    , private TIntegerColumnWriterBase
{
public:
    TUnversionedIntegerColumnWriter(int columnIndex, TDataBlockWriter* blockWriter, int maxValueCount)
        : TColumnWriterBase(blockWriter)
        , ColumnIndex_(columnIndex)
        , MaxValueCount_(maxValueCount)
    {
        Reset();
    }

    void WriteVersionedValues(TRange<TVersionedRow> rows) override
    {
        DoWriteValues(rows);
    }

    void WriteUnversionedValues(TRange<TUnversionedRow> rows) override
    {
        DoWriteValues(rows);
    }

    i32 GetCurrentSegmentSize() const override
    {
        if (Values_.empty()) {
            return 0;
        } else {
            auto sizes = GetSegmentSizeVector();
            auto minElement = std::min_element(sizes.begin(), sizes.end());
            return *minElement;
        }
    }

    void FinishCurrentSegment() override
    {
        if (Values_.size() > 0) {
            DumpSegment();
            Reset();
        }
    }

private:
    const int ColumnIndex_;
    const int MaxValueCount_;
    i64 RunCount_;

    TBitmapOutput NullBitmap_;

    void Reset()
    {
        TIntegerColumnWriterBase::Reset();

        NullBitmap_ = TBitmapOutput();
        RunCount_ = 0;
    }

    i32 GetSegmentSize(EUnversionedIntegerSegmentType type) const
    {
        switch (type) {
            case EUnversionedIntegerSegmentType::DictionaryRle:
                return
                    CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, DistinctValues_.size()) +
                    CompressedUnsignedVectorSizeInBytes(DistinctValues_.size() + 1, RunCount_) +
                    CompressedUnsignedVectorSizeInBytes(RowCount_, RunCount_);

            case EUnversionedIntegerSegmentType::DirectRle:
                return
                    CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, RunCount_) +
                    CompressedUnsignedVectorSizeInBytes(RowCount_, RunCount_) +
                    RunCount_ / 8; // Null bitmap.

            case EUnversionedIntegerSegmentType::DictionaryDense:
                return
                    CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, DistinctValues_.size()) +
                    CompressedUnsignedVectorSizeInBytes(DistinctValues_.size() + 1, Values_.size());

            case EUnversionedIntegerSegmentType::DirectDense:
                return
                    CompressedUnsignedVectorSizeInBytes(MaxValue_ - MinValue_, Values_.size()) +
                    Values_.size() / 8; // Null bitmap.

            default:
                YT_ABORT();
        }
    }

    TEnumIndexedArray<EUnversionedIntegerSegmentType, i32> GetSegmentSizeVector() const
    {
        TEnumIndexedArray<EUnversionedIntegerSegmentType, i32> sizes;

        for (auto type : TEnumTraits<EUnversionedIntegerSegmentType>::GetDomainValues()) {
            sizes[type] = GetSegmentSize(type);
        }
        return sizes;
    }

    // TODO(lukyan): Rewrite this routines via DumpDirectValues and DumpDictionaryValues
    void DumpDirectRleValues(TSegmentInfo* segmentInfo, NColumnarChunkFormat::TKeyIndexMeta* rawIndexMeta, NColumnarChunkFormat::TIntegerMeta* rawMeta)
    {
        TBitmapOutput rleNullBitmap(RunCount_);
        std::vector<ui64> rowIndexes;
        rowIndexes.reserve(RunCount_);

        ui64 runIndex = 0;
        ui64 runBegin = 0;

        while (runBegin < Values_.size()) {
            ui64 runEnd = runBegin + 1;
            while (runEnd < Values_.size() &&
                Values_[runBegin] == Values_[runEnd] &&
                NullBitmap_[runBegin] == NullBitmap_[runEnd])
            {
                ++runEnd;
            }

            // For null values store data as 0.
            Values_[runIndex] = NullBitmap_[runBegin] ? 0 : Values_[runBegin] - MinValue_;
            rowIndexes.push_back(runBegin);
            rleNullBitmap.Append(NullBitmap_[runBegin]);

            ++runIndex;
            runBegin = runEnd;
        }

        YT_VERIFY(static_cast<i64>(runIndex) == RunCount_);
        Values_.resize(RunCount_);

        rawMeta->BaseValue = MinValue_;
        rawMeta->Direct = true;

        // 1. Compressed vector of values.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(Values_), MaxValue_ - MinValue_, &rawMeta->ValuesSize, &rawMeta->ValuesWidth));

        // 2. Null bitmap of values.
        segmentInfo->Data.push_back(rleNullBitmap.Flush<TSegmentWriterTag>());

        // 3. Compressed vector of row indexes.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(rowIndexes), rowIndexes.back(), &rawIndexMeta->RowIndexesSize, &rawIndexMeta->RowIndexesWidth));
    }

    void DumpDictionaryRleValues(TSegmentInfo* segmentInfo, NColumnarChunkFormat::TKeyIndexMeta* rawIndexMeta, NColumnarChunkFormat::TIntegerMeta* rawMeta)
    {
        std::vector<ui64> dictionary;
        dictionary.reserve(DistinctValues_.size());

        std::vector<ui64> rowIndexes;
        rowIndexes.reserve(RunCount_);

        ui64 runIndex = 0;
        ui64 runBegin = 0;
        while (runBegin < Values_.size()) {
            ui64 runEnd = runBegin + 1;
            while (runEnd < Values_.size() &&
                Values_[runBegin] == Values_[runEnd] &&
                NullBitmap_[runBegin] == NullBitmap_[runEnd])
            {
                ++runEnd;
            }

            if (NullBitmap_[runBegin]) {
                Values_[runIndex] = 0;
            } else {
                auto dictionaryIndex = GetOrCrash(DistinctValues_, Values_[runBegin]);
                YT_ASSERT(dictionaryIndex <= std::ssize(dictionary) + 1);

                if (dictionaryIndex > std::ssize(dictionary)) {
                    dictionary.push_back(Values_[runBegin] - MinValue_);
                }

                Values_[runIndex] = dictionaryIndex;
            }

            rowIndexes.push_back(runBegin);

            ++runIndex;
            runBegin = runEnd;
        }

        YT_VERIFY(static_cast<i64>(runIndex) == RunCount_);
        Values_.resize(RunCount_);

        rawMeta->BaseValue = MinValue_;
        rawMeta->Direct = false;

        // 1. Dictionary - compressed vector of values.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(dictionary), MaxValue_ - MinValue_, &rawMeta->ValuesSize, &rawMeta->ValuesWidth));

        // 2. Compressed vector of value ids.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(Values_), dictionary.size() + 1, &rawMeta->IdsSize, &rawMeta->IdsWidth));

        // 3. Compressed vector of row indexes.
        segmentInfo->Data.push_back(BitpackVector(MakeRange(rowIndexes), rowIndexes.back(), &rawIndexMeta->RowIndexesSize, &rawIndexMeta->RowIndexesWidth));
    }

    void DumpSegment()
    {
        for (i64 index = 0; index < std::ssize(Values_); ++index) {
            if (NullBitmap_[index]) {
                YT_ASSERT(Values_[index] == 0);
            } else {
                UpdateStatistics(Values_[index]);
            }
        }

        auto sizes = GetSegmentSizeVector();

        auto minElement = std::min_element(sizes.begin(), sizes.end());
        auto type = EUnversionedIntegerSegmentType(std::distance(sizes.begin(), minElement));

        NColumnarChunkFormat::TKeyMeta<EValueType::Int64> rawMeta;
        memset(&rawMeta, 0, sizeof(rawMeta));
        rawMeta.DataOffset = TColumnWriterBase::GetOffset();
        rawMeta.RowCount = Values_.size();
        rawMeta.ChunkRowCount = RowCount_;

        TSegmentInfo segmentInfo;
        segmentInfo.SegmentMeta.set_type(static_cast<int>(type));
        segmentInfo.SegmentMeta.set_version(0);
        segmentInfo.SegmentMeta.set_row_count(Values_.size());

        auto* meta = segmentInfo.SegmentMeta.MutableExtension(NProto::TIntegerSegmentMeta::integer_segment_meta);
        meta->set_min_value(MinValue_);

        switch (type) {
            case EUnversionedIntegerSegmentType::DirectRle:
                rawMeta.Dense = false;
                DumpDirectRleValues(&segmentInfo, &rawMeta, &rawMeta);
                break;

            case EUnversionedIntegerSegmentType::DictionaryRle:
                rawMeta.Dense = false;
                DumpDictionaryRleValues(&segmentInfo, &rawMeta, &rawMeta);
                break;

            case EUnversionedIntegerSegmentType::DirectDense:
                rawMeta.Dense = true;
                DumpDirectValues(&segmentInfo, NullBitmap_, &rawMeta);
                break;

            case EUnversionedIntegerSegmentType::DictionaryDense:
                rawMeta.Dense = true;
                DumpDictionaryValues(&segmentInfo, NullBitmap_, &rawMeta);
                break;

            default:
                YT_ABORT();
        }

        TColumnWriterBase::DumpSegment(&segmentInfo, TSharedRef::MakeCopy<TSegmentWriterTag>(MetaToRef(rawMeta)));

        if (BlockWriter_->GetEnableSegmentMetaInBlocks()) {
            VerifyRawSegmentMeta(segmentInfo.SegmentMeta, segmentInfo.Data, rawMeta);
        }
    }

    template <class TRow>
    void DoWriteValues(TRange<TRow> rows)
    {
        AddValues(rows);
    }

    template <class TRow>
    void AddValues(TRange<TRow> rows)
    {
        for (auto row : rows) {
            const auto& value = GetUnversionedValue(row, ColumnIndex_);
            bool isNull = value.Type == EValueType::Null;
            ui64 data = 0;
            if (!isNull) {
                data = EncodeValue(GetValue<TValue>(value));
            }

            if (Values_.empty() ||
                NullBitmap_[Values_.size() - 1] != isNull ||
                Values_.back() != data)
            {
                ++RunCount_;
            }

            Values_.push_back(data);
            NullBitmap_.Append(isNull);
            ++RowCount_;

            if (std::ssize(Values_) >= MaxValueCount_) {
                FinishCurrentSegment();
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IValueColumnWriter> CreateUnversionedInt64ColumnWriter(
    int columnIndex,
    TDataBlockWriter* dataBlockWriter,
    int maxValueCount)
{
    return std::make_unique<TUnversionedIntegerColumnWriter<i64>>(
        columnIndex,
        dataBlockWriter,
        maxValueCount);
}

std::unique_ptr<IValueColumnWriter> CreateUnversionedUint64ColumnWriter(
    int columnIndex,
    TDataBlockWriter* dataBlockWriter,
    int maxValueCount)
{
    return std::make_unique<TUnversionedIntegerColumnWriter<ui64>>(
        columnIndex,
        dataBlockWriter,
        maxValueCount);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableChunkFormat
