#pragma once

#include "client.h"

#include <yt/yt/ytlib/transaction_client/public.h>

#include <yt/yt/client/api/client.h>
#include <yt/yt/client/api/transaction.h>

namespace NYT::NApi::NNative {

////////////////////////////////////////////////////////////////////////////////

struct ITransaction
    : public IClientBase
    , public virtual NApi::ITransaction
{
    virtual void AddAction(
        NElection::TCellId cellId,
        const NTransactionClient::TTransactionActionData& data) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITransaction)

////////////////////////////////////////////////////////////////////////////////

ITransactionPtr CreateTransaction(
    IClientPtr client,
    NTransactionClient::TTransactionPtr transaction,
    const NLogging::TLogger& logger);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
