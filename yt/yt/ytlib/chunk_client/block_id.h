#pragma once

#include "public.h"

#include <yt/yt/ytlib/chunk_client/proto/block_id.pb.h>

#include <library/cpp/yt/string/format.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

//! Identifies a block.
/*!
 *  Each block is identified by its chunk id and block index (0-based).
 */
struct TBlockId
{
    TBlockId();
    TBlockId(TChunkId chunkId, int blockIndex);

    //! TChunkId of the chunk where the block belongs.
    TChunkId ChunkId;

    //! An offset where the block starts.
    int BlockIndex;

    bool operator== (const TBlockId& other) const = default;
};

void FormatValue(TStringBuilderBase* builder, const TBlockId& id, TStringBuf spec);

void ToProto(NProto::TBlockId* protoBlockId, const TBlockId& blockId);
void FromProto(TBlockId* blockId, const NProto::TBlockId& protoBlockId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient

//! A hasher for TBlockId.
template <>
struct THash<NYT::NChunkClient::TBlockId>
{
    size_t operator()(const NYT::NChunkClient::TBlockId& blockId) const
    {
        return THash<NYT::TGuid>()(blockId.ChunkId) * 497 +
            blockId.BlockIndex;
    }
};

////////////////////////////////////////////////////////////////////////////////
