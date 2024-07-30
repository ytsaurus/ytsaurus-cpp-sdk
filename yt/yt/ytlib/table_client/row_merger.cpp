#include "row_merger.h"

#include <yt/yt/library/query/engine_api/column_evaluator.h>

#include <yt/yt/client/transaction_client/helpers.h>

#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/schema.h>

namespace NYT::NTableClient {

using namespace NTransactionClient;
using namespace NQueryClient;

////////////////////////////////////////////////////////////////////////////////

TSchemafulRowMerger::TSchemafulRowMerger(
    TRowBufferPtr rowBuffer,
    int columnCount,
    int keyColumnCount,
    const TColumnFilter& columnFilter,
    TColumnEvaluatorPtr columnEvaluator,
    TTimestamp retentionTimestamp,
    const TTimestampColumnMapping& timestampColumnMapping)
    : RowBuffer_(rowBuffer)
    , KeyColumnCount_(keyColumnCount)
    , ColumnEvaluator_(std::move(columnEvaluator))
    , RetentionTimestamp_(retentionTimestamp)
{
    ColumnIdToTimestampColumnId_.assign(static_cast<size_t>(columnCount), -1);
    for (auto [columnId, timestampColumnId] : timestampColumnMapping) {
        ColumnIdToTimestampColumnId_[columnId] = timestampColumnId;
    }

    if (columnFilter.IsUniversal()) {
        ColumnIds_.resize(columnCount);
        std::iota(ColumnIds_.begin(), ColumnIds_.end(), 0);
    } else {
        ColumnIds_ = columnFilter.GetIndexes();
    }

    ColumnIdToIndex_.assign(static_cast<size_t>(columnCount), -1);
    for (int columnIndex = 0; columnIndex < std::ssize(ColumnIds_); ++columnIndex) {
        ColumnIdToIndex_[ColumnIds_[columnIndex]] = columnIndex;
    }

    MergedTimestamps_.resize(std::ssize(ColumnIds_));

    Cleanup();
}

void TSchemafulRowMerger::AddPartialRow(TVersionedRow row)
{
    if (!row) {
        return;
    }

    YT_ASSERT(row.GetWriteTimestampCount() <= 1);
    YT_ASSERT(row.GetDeleteTimestampCount() <= 1);

    AddPartialRow(row, MaxTimestamp);
}

void TSchemafulRowMerger::AddPartialRow(TVersionedRow row, TTimestamp upperTimestampLimit)
{
    if (!row) {
        return;
    }

    if (upperTimestampLimit < RetentionTimestamp_) {
        return;
    }

    if (!Started_) {
        if (!MergedRow_) {
            MergedRow_ = RowBuffer_->AllocateUnversioned(ColumnIds_.size());
        }

        for (int columnIndex = 0; columnIndex < std::ssize(ColumnIds_); ++columnIndex) {
            if (int columnId = ColumnIds_[columnIndex]; columnId >= KeyColumnCount_) {
                MergedTimestamps_[columnIndex] = NullTimestamp;
                MergedRow_[columnIndex] = MakeUnversionedNullValue(columnId);
            }
        }

        for (auto key : row.Keys()) {
            if (int columnIndex = ColumnIdToIndex_[key.Id]; columnIndex != -1) {
                MergedTimestamps_[columnIndex] = MaxTimestamp;
                MergedRow_[columnIndex] = key;
            }
        }

        Started_ = true;
    }

    for (auto timestamp : row.DeleteTimestamps()) {
        if (timestamp < upperTimestampLimit && timestamp >= RetentionTimestamp_) {
            LatestDelete_ = std::max(LatestDelete_, timestamp);
            break;
        }
    }

    for (auto timestamp : row.WriteTimestamps()) {
        if (timestamp < upperTimestampLimit && timestamp >= LatestDelete_ && timestamp >= RetentionTimestamp_) {
            LatestWrite_ = std::max(LatestWrite_, timestamp);
            break;
        }
    }

    for (const auto& partialValue : row.Values()) {
        if (partialValue.Timestamp >= upperTimestampLimit) {
            continue;
        }
        if (partialValue.Timestamp > LatestDelete_ && partialValue.Timestamp >= RetentionTimestamp_) {
            int columnId = partialValue.Id;
            if (int columnIndex = ColumnIdToIndex_[columnId]; columnIndex >= 0) {
                if (ColumnEvaluator_->IsAggregate(columnId)) {
                    AggregateValues_.push_back(partialValue);
                } else if (MergedTimestamps_[columnIndex] < partialValue.Timestamp) {
                    MergedRow_[columnIndex] = partialValue;
                    MergedTimestamps_[columnIndex] = partialValue.Timestamp;
                }
            }

            if (int timestampColumnId = ColumnIdToTimestampColumnId_[columnId]; timestampColumnId >= 0) {
                int timestampColumnIndex = ColumnIdToIndex_[timestampColumnId];

                if (partialValue.Timestamp > MergedTimestamps_[timestampColumnIndex]) {
                    MergedRow_[timestampColumnIndex] = MakeUnversionedUint64Value(partialValue.Timestamp, timestampColumnId);
                    MergedTimestamps_[timestampColumnIndex] = partialValue.Timestamp;
                }
            }
        }
    }
}

TMutableUnversionedRow TSchemafulRowMerger::BuildMergedRow()
{
    if (!Started_) {
        return {};
    }

    if (LatestWrite_ == NullTimestamp || LatestWrite_ < LatestDelete_) {
        Cleanup();
        return {};
    }

    AggregateValues_.erase(
        std::remove_if(
            AggregateValues_.begin(),
            AggregateValues_.end(),
            [latestDelete = LatestDelete_] (const TVersionedValue& value) {
                return value.Timestamp <= latestDelete;
            }),
        AggregateValues_.end());

    std::sort(
        AggregateValues_.begin(),
        AggregateValues_.end(),
        [] (const TVersionedValue& lhs, const TVersionedValue& rhs) {
            return std::tie(lhs.Id, lhs.Timestamp) < std::tie(rhs.Id, rhs.Timestamp);
        });

    AggregateValues_.erase(
        std::unique(
            AggregateValues_.begin(),
            AggregateValues_.end(),
            [] (const TVersionedValue& lhs, const TVersionedValue& rhs) {
                return std::tie(lhs.Id, lhs.Timestamp) == std::tie(rhs.Id, rhs.Timestamp);
            }),
        AggregateValues_.end());

    for (auto it = AggregateValues_.begin(), end = AggregateValues_.end(); it != end;) {
        int columnId = it->Id;

        // Find first element with different id.
        auto next = it;
        while (++next != end && columnId == next->Id) {
            if (None(next->Flags & EValueFlags::Aggregate)) {
                // Skip older aggregate values.
                it = next;
            }
        }

        auto state = *it++;
        while (it != next) {
            ColumnEvaluator_->MergeAggregate(columnId, &state, *it, RowBuffer_);
            ++it;
        }

        TUnversionedValue finalizedState{};
        ColumnEvaluator_->FinalizeAggregate(columnId, &finalizedState, state, RowBuffer_);

        auto columnIndex = ColumnIdToIndex_[columnId];
        MergedTimestamps_[columnIndex] = (it - 1)->Timestamp;
        MergedRow_[columnIndex] = finalizedState;
    }

    for (int columnIndex = 0; columnIndex < std::ssize(ColumnIds_); ++columnIndex) {
        int columnId = ColumnIds_[columnIndex];
        if (MergedTimestamps_[columnIndex] < LatestDelete_ && !ColumnEvaluator_->IsAggregate(columnId)) {
            MergedRow_[columnIndex] = MakeUnversionedNullValue(columnId);
        }
    }

    auto mergedRow = MergedRow_;

    Cleanup();
    return mergedRow;
}

void TSchemafulRowMerger::Reset()
{
    YT_ASSERT(!Started_);
    RowBuffer_->Clear();
    MergedRow_ = {};
}

void TSchemafulRowMerger::Cleanup()
{
    MergedRow_ = {};
    AggregateValues_.clear();
    LatestWrite_ = NullTimestamp;
    LatestDelete_ = NullTimestamp;
    Started_ = false;
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedRowMerger::TUnversionedRowMerger(
    TRowBufferPtr rowBuffer,
    int columnCount,
    int keyColumnCount,
    TColumnEvaluatorPtr columnEvaluator)
    : RowBuffer_(std::move(rowBuffer))
    , ColumnCount_(columnCount)
    , KeyColumnCount_(keyColumnCount)
    , ColumnEvaluator_(std::move(columnEvaluator))
    , ValidValues_(size_t(ColumnCount_) - KeyColumnCount_, false)
{
    YT_VERIFY(KeyColumnCount_ <= ColumnCount_);
}

void TUnversionedRowMerger::InitPartialRow(TUnversionedRow row)
{
    MergedRow_ = RowBuffer_->AllocateUnversioned(ColumnCount_);

    ValidValues_.assign(ColumnCount_ - KeyColumnCount_, false);

    std::copy(row.begin(), row.begin() + KeyColumnCount_, MergedRow_.begin());

    for (int index = KeyColumnCount_; index < ColumnCount_; ++index) {
        auto flags = EValueFlags::None;
        if (ColumnEvaluator_->IsAggregate(index)) {
            flags |= EValueFlags::Aggregate;
        }
        MergedRow_[index] = MakeUnversionedNullValue(index, flags);
    }
}

void TUnversionedRowMerger::AddPartialRow(TUnversionedRow row)
{
    YT_VERIFY(row);

    for (int partialIndex = KeyColumnCount_; partialIndex < static_cast<int>(row.GetCount()); ++partialIndex) {
        const auto& partialValue = row[partialIndex];
        int id = partialValue.Id;
        YT_VERIFY(id >= KeyColumnCount_);
        ValidValues_[id - KeyColumnCount_] = true;
        auto& mergedValue = MergedRow_[id];
        if (Any(partialValue.Flags & EValueFlags::Aggregate)) {
            YT_VERIFY(ColumnEvaluator_->IsAggregate(id));
            bool isAggregate = Any(mergedValue.Flags & EValueFlags::Aggregate);
            ColumnEvaluator_->MergeAggregate(id, &mergedValue, partialValue, RowBuffer_);
            if (isAggregate) {
                mergedValue.Flags |= EValueFlags::Aggregate;
            }
        } else {
            mergedValue = partialValue;
        }
    }
}

void TUnversionedRowMerger::DeletePartialRow(TUnversionedRow /*row*/)
{
    // NB: Since we don't have delete timestamps here we need to write null into all columns.

    for (int index = KeyColumnCount_; index < ColumnCount_; ++index) {
        ValidValues_[index - KeyColumnCount_] = true;
        MergedRow_[index] = MakeUnversionedNullValue(index);
    }
}

TMutableUnversionedRow TUnversionedRowMerger::BuildDeleteRow()
{
    auto mergedRow = MergedRow_;
    mergedRow.SetCount(KeyColumnCount_);
    MergedRow_ = {};
    return mergedRow;
}

TMutableUnversionedRow TUnversionedRowMerger::BuildMergedRow()
{
    bool fullRow = true;
    for (bool validValue : ValidValues_) {
        if (!validValue) {
            fullRow = false;
            break;
        }
    }

    TMutableUnversionedRow mergedRow;

    if (fullRow) {
        mergedRow = MergedRow_;
    } else {
        mergedRow = RowBuffer_->AllocateUnversioned(ColumnCount_);

        TUnversionedValue* it = MergedRow_.begin() + KeyColumnCount_;
        auto jt = std::copy(MergedRow_.begin(), it, mergedRow.begin());

        YT_VERIFY(static_cast<int>(MergedRow_.GetCount()) == ColumnCount_);
        for (bool isValid : ValidValues_) {
            if (isValid) {
                *jt++ = *it;
            }
            ++it;
        }

        mergedRow.SetCount(jt - mergedRow.begin());
    }

    MergedRow_ = TMutableUnversionedRow();
    return mergedRow;
}

////////////////////////////////////////////////////////////////////////////////

TSamplingRowMerger::TSamplingRowMerger(
    TRowBufferPtr rowBuffer,
    TTableSchemaPtr schema)
    : RowBuffer_(std::move(rowBuffer))
    , KeyColumnCount_(schema->GetKeyColumnCount())
    , LatestTimestamps_(static_cast<size_t>(schema->GetColumnCount()), NullTimestamp)
    , IdMapping_(static_cast<size_t>(schema->GetColumnCount()), -1)
{
    for (const auto& column : schema->Columns()) {
        if (!column.Aggregate()) {
            IdMapping_[schema->GetColumnIndex(column)] = SampledColumnCount_;
            ++SampledColumnCount_;
        }
    }
}

TMutableUnversionedRow TSamplingRowMerger::MergeRow(TVersionedRow row)
{
    auto mergedRow = RowBuffer_->AllocateUnversioned(SampledColumnCount_);

    YT_VERIFY(row.GetKeyCount() == KeyColumnCount_);
    for (int index = 0; index < row.GetKeyCount(); ++index) {
        mergedRow[index] = row.Keys()[index];
    }

    for (int index = row.GetKeyCount(); index < SampledColumnCount_; ++index) {
        mergedRow[index] = MakeUnversionedSentinelValue(EValueType::Null, index);
    }

    auto deleteTimestamp = row.GetDeleteTimestampCount() > 0
        ? row.DeleteTimestamps()[0]
        : NullTimestamp;

    for (const auto& value : row.Values()) {
        if (value.Timestamp < deleteTimestamp || value.Timestamp < LatestTimestamps_[value.Id]) {
            continue;
        }

        auto id = IdMapping_[value.Id];
        if (id != -1) {
            mergedRow[id] = value;
            LatestTimestamps_[id] = value.Timestamp;
        }
    }

    return mergedRow;
}

void TSamplingRowMerger::Reset()
{
    RowBuffer_->Clear();
    std::fill(LatestTimestamps_.begin(), LatestTimestamps_.end(), NullTimestamp);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
