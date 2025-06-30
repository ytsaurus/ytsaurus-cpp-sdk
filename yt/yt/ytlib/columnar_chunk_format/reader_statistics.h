#pragma once

#include <yt/yt/core/misc/common.h>

#include <library/cpp/yt/cpu_clock/clock.h>

namespace NYT::NColumnarChunkFormat {

////////////////////////////////////////////////////////////////////////////////

struct TReaderStatistics final
{
    TCpuDuration InitTime;
    TCpuDuration ReadTime;

    TCpuDuration BuildReadWindowsTime;
    TCpuDuration GetValuesIdMappingTime;
    TCpuDuration CreateColumnBlockHoldersTime;
    TCpuDuration GetTypesFromSchemaTime;
    TCpuDuration BuildColumnInfosTime;
    TCpuDuration CreateRowsetBuilderTime;
    TCpuDuration CreateBlockManagerTime;

    TCpuDuration DecodeTimestampSegmentTime;
    TCpuDuration DecodeKeySegmentTime;
    TCpuDuration DecodeValueSegmentTime;
    TCpuDuration FetchBlockTime;
    TCpuDuration BuildRangesTime;

    TCpuDuration DoReadTime;
    TCpuDuration CollectCountsTime;
    TCpuDuration AllocateRowsTime;
    TCpuDuration DoReadKeysTime;
    TCpuDuration DoReadValuesTime;

    size_t RowCount = 0;

    size_t TryUpdateWindowCallCount = 0;
    size_t SkipToBlockCallCount = 0;
    size_t FetchBlockCallCount = 0;
    size_t SetBlockCallCount = 0;

    size_t UpdateSegmentCallCount = 0;
    size_t DoReadCallCount = 0;
};

using TReaderStatisticsPtr = TIntrusivePtr<TReaderStatistics>;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NColumnarChunkFormat
