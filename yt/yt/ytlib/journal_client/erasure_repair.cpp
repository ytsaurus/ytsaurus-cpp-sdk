#include "erasure_repair.h"
#include "erasure_parts_reader.h"

#include "config.h"

#include <yt/yt/client/misc/workload.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/chunk_writer.h>
#include <yt/yt/ytlib/chunk_client/ref_counted_proto.h>
#include <yt/yt/ytlib/chunk_client/dispatcher.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/scheduler.h>

namespace NYT::NJournalClient {

using namespace NErasure;
using namespace NChunkClient;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

namespace {

void DoRepairErasedParts(
    TChunkReaderConfigPtr config,
    ICodec* codec,
    i64 rowCount,
    const TPartIndexList& erasedIndices,
    const std::vector<IChunkReaderPtr>& readers,
    const std::vector<IChunkWriterPtr>& writers,
    const TClientChunkReadOptions& options,
    const NLogging::TLogger& logger)
{
    YT_VERIFY(!writers.empty());
    YT_VERIFY(writers.size() == erasedIndices.size());

    const auto& Logger = logger;

    auto partsReader = New<TErasurePartsReader>(
        std::move(config),
        codec,
        readers,
        erasedIndices,
        logger);

    // Open writers.
    {
        YT_LOG_DEBUG("Started opening writers");

        std::vector<TFuture<void>> futures;
        for (const auto& writer : writers) {
            if (writer) {
                futures.push_back(writer->Open());
            }
        }

        WaitFor(AllSucceeded(std::move(futures)))
            .ThrowOnError();

        YT_LOG_DEBUG("Writers opened");
    }

    // Read/write rows.
    i64 firstRowIndex = 0;
    while (firstRowIndex < rowCount) {
        YT_LOG_DEBUG("Reading rows (Rows: %v-%v)",
            firstRowIndex,
            rowCount - 1);

        auto future = partsReader->ReadRows(
            options,
            firstRowIndex,
            rowCount - firstRowIndex);
        auto rowLists = WaitFor(future)
            .ValueOrThrow();

        YT_VERIFY(rowLists.size() == writers.size());

        i64 readRowCount = static_cast<i64>(rowLists[0].size());
        for (const auto& rowList : rowLists) {
            YT_VERIFY(std::ssize(rowList) == readRowCount);
        }

        YT_LOG_DEBUG("Rows received (Rows: %v-%v)",
            firstRowIndex,
            firstRowIndex + readRowCount - 1);

        std::vector<TFuture<void>> futures;
        std::vector<TBlock> blocks;
        blocks.reserve(rowLists[0].size());
        for (size_t index = 0; index < writers.size(); ++index) {
            const auto& writer = writers[index];
            if (!writer) {
                continue;
            }
            blocks.clear();
            for (auto& row : rowLists[index]) {
                blocks.emplace_back(std::move(row));
            }

            TWorkloadDescriptor workloadDescriptor;
            workloadDescriptor.Category = EWorkloadCategory::SystemRepair;

            if (!writer->WriteBlocks(workloadDescriptor, blocks)) {
                futures.push_back(writer->GetReadyEvent());
            }
        }

        if (!futures.empty()) {
            YT_LOG_DEBUG("Started waiting for writers");

            WaitFor(AllSucceeded(std::move(futures)))
                .ThrowOnError();

            YT_LOG_DEBUG("Finished waiting for writers");
        }

        firstRowIndex += readRowCount;
    }

    // Close writers.
    {
        YT_LOG_DEBUG("Started closing writers");

        std::vector<TFuture<void>> futures;
        for (const auto& writer : writers) {
            if (writer) {
                futures.push_back(writer->Close());
            }
        }

        WaitFor(AllSucceeded(std::move(futures)))
            .ThrowOnError();

        YT_LOG_DEBUG("Writers closed");
    }
}

} // namespace

TFuture<void> RepairErasedParts(
    TChunkReaderConfigPtr config,
    ICodec* codec,
    i64 rowCount,
    const TPartIndexList& erasedIndices,
    std::vector<IChunkReaderPtr> readers,
    std::vector<IChunkWriterPtr> writers,
    TClientChunkReadOptions options,
    NLogging::TLogger logger)
{
    return BIND(&DoRepairErasedParts)
        .AsyncVia(CreateSerializedInvoker(TDispatcher::Get()->GetReaderInvoker()))
        .Run(
            std::move(config),
            codec,
            rowCount,
            erasedIndices,
            std::move(readers),
            std::move(writers),
            std::move(options),
            std::move(logger));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJournalClient

