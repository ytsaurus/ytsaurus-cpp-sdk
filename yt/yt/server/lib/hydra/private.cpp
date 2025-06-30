#include "private.h"
#include "config.h"

namespace NYT::NHydra {

using namespace NConcurrency;
using namespace NElection;

////////////////////////////////////////////////////////////////////////////////

TConfigWrapper::TConfigWrapper(TDistributedHydraManagerConfigPtr config)
    : Config_(config)
{ }

void TConfigWrapper::Set(TDistributedHydraManagerConfigPtr config)
{
    Config_.Store(config);
}

TDistributedHydraManagerConfigPtr TConfigWrapper::Get() const
{
    return Config_.Acquire();
}

////////////////////////////////////////////////////////////////////////////////

bool IsSystemMutationType(const TString& mutationType)
{
    return mutationType == NHydra::HeartbeatMutationType ||
        mutationType == ExitReadOnlyMutationType ||
        mutationType == EnterReadOnlyMutationType;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
