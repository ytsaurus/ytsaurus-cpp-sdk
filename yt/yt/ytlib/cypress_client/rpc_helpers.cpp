#include "rpc_helpers.h"

#include <yt/yt/ytlib/cypress_client/proto/rpc.pb.h>

#include <yt/yt/core/rpc/client.h>
#include <yt/yt/core/rpc/service.h>

namespace NYT::NCypressClient {

using namespace NRpc;
using namespace NRpc::NProto;
using namespace NCypressClient::NProto;

////////////////////////////////////////////////////////////////////////////////

TTransactionId GetTransactionId(const IServiceContextPtr& context)
{
    return GetTransactionId(context->RequestHeader());
}

TTransactionId GetTransactionId(const TRequestHeader& header)
{
    return FromProto<TTransactionId>(header.GetExtension(TTransactionalExt::transaction_id));
}

void SetTransactionId(const IClientRequestPtr& request, TTransactionId transactionId)
{
    SetTransactionId(&request->Header(), transactionId);
}

void SetTransactionId(TRequestHeader* header, TTransactionId transactionId)
{
    ToProto(header->MutableExtension(TTransactionalExt::transaction_id), transactionId);
}

void SetSuppressAccessTracking(const IClientRequestPtr& request, bool value)
{
    SetSuppressAccessTracking(&request->Header(), value);
}

void SetSuppressAccessTracking(TRequestHeader* header, bool value)
{
    header->SetExtension(TAccessTrackingExt::suppress_access_tracking, value);
}

bool GetSuppressAccessTracking(const TRequestHeader& header)
{
    return header.HasExtension(TAccessTrackingExt::suppress_access_tracking)
        ? header.GetExtension(TAccessTrackingExt::suppress_access_tracking)
        : false;
}

void SetSuppressModificationTracking(const IClientRequestPtr& request, bool value)
{
    SetSuppressModificationTracking(&request->Header(), value);
}

void SetSuppressModificationTracking(TRequestHeader* header, bool value)
{
    header->SetExtension(TAccessTrackingExt::suppress_modification_tracking, value);
}

bool GetSuppressModificationTracking(const TRequestHeader& header)
{
    return header.HasExtension(TAccessTrackingExt::suppress_modification_tracking)
        ? header.GetExtension(TAccessTrackingExt::suppress_modification_tracking)
        : false;
}

void SetSuppressExpirationTimeoutRenewal(const NRpc::IClientRequestPtr& request, bool value)
{
    SetSuppressExpirationTimeoutRenewal(&request->Header(), value);
}

void SetSuppressExpirationTimeoutRenewal(NRpc::NProto::TRequestHeader* header, bool value)
{
    header->SetExtension(TAccessTrackingExt::suppress_expiration_timeout_renewal, value);
}

bool GetSuppressExpirationTimeoutRenewal(const NRpc::NProto::TRequestHeader& header)
{
    return header.HasExtension(TAccessTrackingExt::suppress_expiration_timeout_renewal)
        ? header.GetExtension(TAccessTrackingExt::suppress_expiration_timeout_renewal)
        : false;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressClient

