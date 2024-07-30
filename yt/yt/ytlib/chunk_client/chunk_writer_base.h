#pragma once

#include "chunk_meta_extensions.h"

#include <yt/yt/client/chunk_client/writer_base.h>
#include <yt/yt/client/chunk_client/data_statistics.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

struct IChunkWriterBase
    : public virtual IWriterBase
{
    virtual i64 GetMetaSize() const = 0;
    virtual i64 GetCompressedDataSize() const = 0;
    virtual i64 GetDataWeight() const = 0;

    // Exposes writer internal wish to be closed; e.g. partition chunk writer may
    // want to be closed if some partition row count is close to i32 limit.
    virtual bool IsCloseDemanded() const = 0;

    virtual TDeferredChunkMetaPtr GetMeta() const = 0;
    virtual TChunkId GetChunkId() const = 0;

    virtual NProto::TDataStatistics GetDataStatistics() const = 0;
    virtual TCodecStatistics GetCompressionStatistics() const = 0;
};

DEFINE_REFCOUNTED_TYPE(IChunkWriterBase)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
