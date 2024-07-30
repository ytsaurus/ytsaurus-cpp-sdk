#ifndef ACTION_INL_H_
#error "Direct inclusion of this file is not allowed, include action.h"
// For the sake of sane code completion.
#include "action.h"
#endif

#include <yt/yt/core/misc/protobuf_helpers.h>

#include <yt/yt/core/misc/serialize.h>

namespace NYT::NTransactionClient {

////////////////////////////////////////////////////////////////////////////////

template <class TProto>
TTransactionActionData MakeTransactionActionData(const TProto& message)
{
    return TTransactionActionData{
        message.GetTypeName(),
        SerializeProtoToString(message)
    };
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionClient
