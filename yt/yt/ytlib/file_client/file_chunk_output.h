#pragma once

#include "public.h"
#include "chunk_meta_extensions.h"

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/client/chunk_client/chunk_replica.h>
#include <yt/yt/ytlib/chunk_client/public.h>
#include <yt/yt/ytlib/chunk_client/data_sink.h>

#include <yt/yt/core/concurrency/public.h>

#include <yt/yt/core/compression/codec.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/rpc/public.h>

namespace NYT::NFileClient {

////////////////////////////////////////////////////////////////////////////////

//! A client-side facade for writing files.
/*!
 *  The client must call #Open and then feed the data in by calling #Write.
 *  Finally it must call #Finish.
 */
class TFileChunkOutput
    : public IOutputStream
{
public:
    //! Initializes an instance.
    TFileChunkOutput(
        NApi::TFileWriterConfigPtr config,
        NChunkClient::TMultiChunkWriterOptionsPtr options,
        NApi::NNative::IClientPtr client,
        NTransactionClient::TTransactionId transactionId,
        NChunkClient::TDataSink dataSink,
        NChunkClient::TTrafficMeterPtr trafficMeter,
        NConcurrency::IThroughputThrottlerPtr throttler,
        i64 sizeLimit = std::numeric_limits<i64>::max());

    NChunkClient::TChunkId GetChunkId() const;

protected:
    //! Adds another portion of data.
    /*!
     *  This portion does not necessary makes up a block. The writer maintains an internal buffer
     *  and splits the input data into parts of equal size (see #TConfig::BlockSize).
     */
    void DoWrite(const void* buf, size_t len) override;

    //! Closes the writer.
    void DoFinish() override;

    NLogging::TLogger Logger;

private:
    const NApi::TFileWriterConfigPtr Config_;
    const NChunkClient::TMultiChunkWriterOptionsPtr Options_;
    const NApi::NNative::IClientPtr Client_;
    const NObjectClient::TTransactionId TransactionId_;
    const NChunkClient::TDataSink DataSink_;
    const NChunkClient::TTrafficMeterPtr TrafficMeter_;
    const NConcurrency::IThroughputThrottlerPtr Throttler_;
    const i64 SizeLimit_;

    bool Open_ = false;

    NChunkClient::IChunkWriterPtr ConfirmingChunkWriter_;
    IFileChunkWriterPtr FileChunkWriter_;

    i64 GetSize() const;
    void EnsureOpen();
    void FlushBlock();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NFileClient
