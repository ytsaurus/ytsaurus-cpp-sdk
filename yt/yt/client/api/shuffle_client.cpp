#include "shuffle_client.h"

namespace NYT::NApi {

using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

void TShuffleHandle::Register(TRegistrar registrar)
{
    registrar.Parameter("transaction_id", &TThis::TransactionId);
    registrar.Parameter("coordinator_address", &TThis::CoordinatorAddress);
    registrar.Parameter("account", &TThis::Account);
    registrar.Parameter("partition_count", &TThis::PartitionCount);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi