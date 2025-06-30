#pragma once

#include "public.h"

#include <yt/yt/core/actions/public.h>

#include <yt/yt/core/concurrency/public.h>

#include <yt/yt/core/net/public.h>

#include <library/cpp/yt/memory/ref.h>

namespace NYT::NKafkaProxy {

////////////////////////////////////////////////////////////////////////////////

using TMessage = TSharedRefArray;

//! This handler is called whenever client sends request to server.
using TRequestHandler = TCallback<void(IConnectionPtr, TMessage)>;

//! This handler is called when connection fails.
using TFailHandler = TCallback<void(IConnectionPtr, TError)>;

using TConnectionId = TGuid;

////////////////////////////////////////////////////////////////////////////////

//! Represents established connection to client.
struct IConnection
    : public TSharedRangeHolder
{
    //! Starts listening connection for incoming requests.
    //! This function can be called at most once.
    virtual void Start() = 0;

    //! Terminates connection to client.
    //! This function can be called at most once.
    virtual TFuture<void> Terminate() = 0;

    //! Sends a message to client.
    virtual TFuture<void> PostMessage(TMessage message) = 0;

    //! Returns unique connection id.
    virtual TConnectionId GetConnectionId() const = 0;
};

DEFINE_REFCOUNTED_TYPE(IConnection)

////////////////////////////////////////////////////////////////////////////////

IConnectionPtr CreateConnection(
    TProxyBootstrapConfigPtr config,
    NNet::IConnectionPtr connection,
    IInvokerPtr invoker,
    TRequestHandler requestHandler,
    TFailHandler failHandler);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NKafkaProxy
