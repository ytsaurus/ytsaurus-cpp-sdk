#pragma once

#include <yt/yt/client/election/public.h>

#include <library/cpp/yt/memory/intrusive_ptr.h>

namespace NYT::NElection {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TCellManager)

DECLARE_REFCOUNTED_CLASS(TCellPeerConfig)
DECLARE_REFCOUNTED_CLASS(TCellConfig)

DECLARE_REFCOUNTED_STRUCT(IAlienCellPeerChannelFactory)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NElection
