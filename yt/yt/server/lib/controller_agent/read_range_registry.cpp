#include "read_range_registry.h"

namespace NYT::NControllerAgent {

using namespace NTableClient;
using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

void TReadRangeRegistry::RegisterDataSlice(const TLegacyDataSlicePtr& dataSlice)
{
    YT_VERIFY(!dataSlice->IsLegacy);

    auto readRangeIndex = Ranges_.size();
    dataSlice->ReadRangeIndex = readRangeIndex;
    auto& readRange = Ranges_.emplace_back();
    readRange.LowerBound = dataSlice->LowerLimit().KeyBound;
    readRange.UpperBound = dataSlice->UpperLimit().KeyBound;
}

void TReadRangeRegistry::ApplyReadRange(const TLegacyDataSlicePtr& dataSlice, const TComparator& comparator) const
{
    YT_VERIFY(!dataSlice->IsLegacy);
    YT_VERIFY(dataSlice->ReadRangeIndex);

    const auto& readRange = Ranges_[*dataSlice->ReadRangeIndex];
    comparator.ReplaceIfStrongerKeyBound(dataSlice->LowerLimit().KeyBound, readRange.LowerBound);
    comparator.ReplaceIfStrongerKeyBound(dataSlice->UpperLimit().KeyBound, readRange.UpperBound);

    for (const auto& chunkSlice : dataSlice->ChunkSlices) {
        YT_VERIFY(!chunkSlice->IsLegacy);
        // There is no difference between replacing with dataSlice->LowerLimit().KeyBound and readRange.LowerBound;
        // it should be true that chunk slice ranges already fit into the data slice range.
        comparator.ReplaceIfStrongerKeyBound(chunkSlice->LowerLimit().KeyBound, dataSlice->LowerLimit().KeyBound);
        comparator.ReplaceIfStrongerKeyBound(chunkSlice->UpperLimit().KeyBound, dataSlice->UpperLimit().KeyBound);
    }
}

void TReadRangeRegistry::RegisterMetadata(auto&& registrar)
{
    PHOENIX_REGISTER_FIELD(1, Ranges_);
}

void TReadRangeRegistry::TInputReadRange::RegisterMetadata(auto&& registrar)
{
    PHOENIX_REGISTER_FIELD(1, UpperBound);
    PHOENIX_REGISTER_FIELD(2, LowerBound);
}

PHOENIX_DEFINE_TYPE(TReadRangeRegistry);
PHOENIX_DEFINE_TYPE(TReadRangeRegistry::TInputReadRange);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
