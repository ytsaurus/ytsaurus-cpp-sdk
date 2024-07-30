#include "table_writer.h"
#include "client.h"

#include <yt/yt/client/table_client/name_table.h>

#include <yt/yt/ytlib/table_client/schemaless_chunk_writer.h>
#include <yt/yt/ytlib/table_client/config.h>
#include <yt/yt/ytlib/table_client/helpers.h>

namespace NYT::NApi::NNative {

using namespace NTableClient;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TFuture<ITableWriterPtr> CreateTableWriter(
    const IClientPtr& client,
    const NYPath::TRichYPath& path,
    const TTableWriterOptions& options)
{
    auto nameTable = New<TNameTable>();
    nameTable->SetEnableColumnNameValidation();

    auto writerOptions = New<NTableClient::TTableWriterOptions>();
    writerOptions->EnableValidationOptions(/*validateAnyIsValidYson*/ options.ValidateAnyIsValidYson);

    NApi::ITransactionPtr transaction;
    if (options.TransactionId) {
        TTransactionAttachOptions transactionOptions;
        transactionOptions.Ping = options.Ping;
        transactionOptions.PingAncestors = options.PingAncestors;
        transaction = client->AttachTransaction(options.TransactionId, transactionOptions);
    }

    auto asyncSchemalessWriter = CreateSchemalessTableWriter(
        options.Config ? options.Config : New<TTableWriterConfig>(),
        writerOptions,
        path,
        nameTable,
        client,
        /*localHostName*/ TString(), // Locality is not important during table upload.
        transaction);

    return asyncSchemalessWriter.Apply(BIND([] (const IUnversionedWriterPtr& schemalessWriter) {
        return CreateApiFromSchemalessWriterAdapter(std::move(schemalessWriter));
    }));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative

