#include "chaos_cell_bundle.h"

#include "chaos_cell.h"
#include "config.h"

namespace NYT::NChaosServer {

using namespace NCellMaster;
using namespace NObjectServer;
using namespace NChunkClient;
using namespace NYson;
using namespace NYTree;
using namespace NCellServer;

////////////////////////////////////////////////////////////////////////////////

TChaosCellBundle::TChaosCellBundle(TChaosCellBundleId id)
    : TCellBundle(id)
    , ChaosOptions_(New<TChaosHydraConfig>())
{ }

void TChaosCellBundle::RemoveMetadataCell(TChaosCell* cell)
{
    std::erase(MetadataCells_, cell);
}

void TChaosCellBundle::Save(NCellMaster::TSaveContext& context) const
{
    TCellBundle::Save(context);

    using NYT::Save;
    Save(context, *ChaosOptions_);
    Save(context, MetadataCells_);
}

void TChaosCellBundle::Load(NCellMaster::TLoadContext& context)
{
    TCellBundle::Load(context);

    using NYT::Load;
    Load(context, *ChaosOptions_);
    Load(context, MetadataCells_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChaosServer
