#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra/mutation.h>

#include <yt/yt/ytlib/election/public.h>

#include <yt/yt/core/rpc/public.h>

namespace NYT::NClusterClock {

////////////////////////////////////////////////////////////////////////////////

class THydraFacade
    : public TRefCounted
{
public:
    THydraFacade(
        TClusterClockBootstrapConfigPtr config,
        TBootstrap* bootstrap);
    ~THydraFacade();

    void Initialize();
    void LoadSnapshot(
        NHydra::ISnapshotReaderPtr reader,
        ESerializationDumpMode dumpMode);

    const TClockAutomatonPtr& GetAutomaton() const;
    const NElection::IElectionManagerPtr& GetElectionManager() const;
    const NHydra::IHydraManagerPtr& GetHydraManager() const;
    const NRpc::IResponseKeeperPtr& GetResponseKeeper() const;

    IInvokerPtr GetAutomatonInvoker(EAutomatonThreadQueue queue) const;
    IInvokerPtr GetEpochAutomatonInvoker(EAutomatonThreadQueue queue) const;
    IInvokerPtr GetGuardedAutomatonInvoker(EAutomatonThreadQueue queue) const;

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;

};

DEFINE_REFCOUNTED_TYPE(THydraFacade)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClusterClock
