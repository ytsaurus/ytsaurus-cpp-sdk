#ifndef TRAFFIC_METER_INL_H_
#error "Direct inclusion of this file is not allowed, include traffic_meter.h"
// For the sake of sane code completion.
#include "traffic_meter.h"
#endif

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

template <typename T>
void TTrafficMeter::IncrementByteCountImpl(THashMap<T, i64>& data, const T& key, i64 byteCount)
{
    auto guard = Guard(Lock_);
    data[key] += byteCount;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
