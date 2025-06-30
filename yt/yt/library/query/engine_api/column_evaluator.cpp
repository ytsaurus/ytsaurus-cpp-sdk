#include "column_evaluator.h"

#include <yt/yt/client/table_client/row_buffer.h>

namespace NYT::NQueryClient {

using NTableClient::TMutableVersionedRow;
using NTableClient::TMutableUnversionedRow;
using NTableClient::TUnversionedValue;

////////////////////////////////////////////////////////////////////////////////

Y_WEAK TColumnEvaluatorPtr TColumnEvaluator::Create(
    const TTableSchemaPtr& /*schema*/,
    const TConstTypeInferrerMapPtr& /*typeInferrers*/,
    const TConstFunctionProfilerMapPtr& /*profilers*/)
{
    // Proper implementation resides in yt/yt/library/query/engine/column_evaluator.cpp.
    YT_ABORT();
}

////////////////////////////////////////////////////////////////////////////////

TColumnEvaluator::TColumnEvaluator(
    std::vector<TColumn> columns,
    std::vector<bool> isAggregate)
    : Columns_(std::move(columns))
    , IsAggregate_(std::move(isAggregate))
{ }

void TColumnEvaluator::EvaluateKey(TMutableRow fullRow, const TRowBufferPtr& buffer, int columnIndex, bool preserveColumnId) const
{
    YT_VERIFY(columnIndex < static_cast<int>(fullRow.GetCount()));
    YT_VERIFY(columnIndex < std::ssize(Columns_));

    const auto& column = Columns_[columnIndex];
    auto& evaluator = column.EvaluatorInstance;
    YT_VERIFY(evaluator);

    auto initialColumnId = fullRow[columnIndex].Id;

    // Zero row to avoid garbage after evaluator.
    fullRow[columnIndex] = MakeUnversionedSentinelValue(EValueType::Null);

    evaluator.Run(
        column.Variables.GetLiteralValues(),
        column.Variables.GetOpaqueData(),
        column.Variables.GetOpaqueDataSizes(),
        &fullRow[columnIndex],
        fullRow.Elements(),
        buffer);

    fullRow[columnIndex].Id = preserveColumnId ? initialColumnId : columnIndex;
}

void TColumnEvaluator::EvaluateKeys(
    TMutableRow fullRow,
    const TRowBufferPtr& buffer,
    bool preserveColumnsIds) const
{
    for (int index = 0; index < std::ssize(Columns_); ++index) {
        if (Columns_[index].EvaluatorImage) {
            EvaluateKey(fullRow, buffer, index, preserveColumnsIds);
        }
    }
}

void TColumnEvaluator::EvaluateKeys(
    TMutableVersionedRow fullRow,
    const TRowBufferPtr& buffer,
    bool preserveColumnsIds) const
{
    auto row = buffer->CaptureRow(fullRow.Keys(), /*captureValues*/ false);
    EvaluateKeys(row, buffer, preserveColumnsIds);

    for (int index = 0; index < fullRow.GetKeyCount(); ++index) {
        if (Columns_[index].EvaluatorImage) {
            fullRow.Keys()[index] = row[index];
        }
    }
}

const std::vector<int>& TColumnEvaluator::GetReferenceIds(int index) const
{
    return Columns_[index].ReferenceIds;
}

TConstExpressionPtr TColumnEvaluator::GetExpression(int index) const
{
    return Columns_[index].Expression;
}

void TColumnEvaluator::InitAggregate(
    int index,
    TUnversionedValue* state,
    const TRowBufferPtr& buffer) const
{
    Columns_[index].AggregateInstance.RunInit(buffer, state);
    state->Id = index;
}

void TColumnEvaluator::UpdateAggregate(
    int index,
    TUnversionedValue* state,
    const TRange<TUnversionedValue> update,
    const TRowBufferPtr& buffer) const
{
    Columns_[index].AggregateInstance.RunUpdate(buffer, state, update);
    state->Id = index;
}

void TColumnEvaluator::MergeAggregate(
    int index,
    TUnversionedValue* state,
    const TUnversionedValue& mergeeState,
    const TRowBufferPtr& buffer) const
{
    Columns_[index].AggregateInstance.RunMerge(buffer, state, &mergeeState);
    state->Id = index;
}

void TColumnEvaluator::FinalizeAggregate(
    int index,
    TUnversionedValue* result,
    const TUnversionedValue& state,
    const TRowBufferPtr& buffer) const
{
    Columns_[index].AggregateInstance.RunFinalize(buffer, result, &state);
    result->Id = index;
}

////////////////////////////////////////////////////////////////////////////////

Y_WEAK IColumnEvaluatorCachePtr CreateColumnEvaluatorCache(
    TColumnEvaluatorCacheConfigPtr /*config*/,
    TConstTypeInferrerMapPtr /*typeInferrers*/,
    TConstFunctionProfilerMapPtr /*profilers*/)
{
    // Proper implementation resides in yt/yt/library/query/engine/column_evaluator.cpp.
    YT_ABORT();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
