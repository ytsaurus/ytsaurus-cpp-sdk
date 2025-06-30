#include "mutation_forwarder.h"

#include "tablet_manager.h"
#include "tablet.h"

#include <yt/yt/server/lib/hive/hive_manager.h>

#include <google/protobuf/message.h>

namespace NYT::NTabletNode {

using namespace NHiveServer;
using namespace NObjectClient;

////////////////////////////////////////////////////////////////////////////////

class TMutationForwarder
    : public IMutationForwarder
{
public:
    TMutationForwarder(
        TWeakPtr<ITabletManager> tabletManager,
        IHiveManagerPtr hiveManager)
        : TabletManager_(std::move(tabletManager))
        , HiveManager_(std::move(hiveManager))
    { }

    void MaybeForwardMutationToSiblingServant(
        const TTablet* tablet,
        const ::google::protobuf::Message& message) override
    {
        const auto& movementData = tablet->SmoothMovementData();
        if (movementData.ShouldForwardMutation()) {
            auto endpointId = movementData.GetSiblingAvenueEndpointId();
            auto mailbox = HiveManager_->GetMailbox(endpointId);
            HiveManager_->PostMessage(mailbox, message);
        }
    }

    void MaybeForwardMutationToSiblingServant(
        TTabletId tabletId,
        const ::google::protobuf::Message& message) override
    {
        auto tabletManager = TabletManager_.Lock();
        if (!tabletManager) {
            return;
        }

        const auto* tablet = tabletManager->FindTablet(tabletId);
        if (!tablet) {
            return;
        }

        MaybeForwardMutationToSiblingServant(tablet, message);
    }

private:
    const TWeakPtr<ITabletManager> TabletManager_;
    const IHiveManagerPtr HiveManager_;
};

////////////////////////////////////////////////////////////////////////////////

IMutationForwarderPtr CreateMutationForwarder(
    TWeakPtr<ITabletManager> tabletManager,
    IHiveManagerPtr hiveManager)
{
    return New<TMutationForwarder>(
        std::move(tabletManager),
        std::move(hiveManager));
}

IMutationForwarderPtr CreateDummyMutationForwarder()
{
    class TDummyMutationForwarder
        : public IMutationForwarder
    {
    public:
        void MaybeForwardMutationToSiblingServant(
            TTabletId /*tabletId*/,
            const ::google::protobuf::Message& /*message*/) override
        { }

        void MaybeForwardMutationToSiblingServant(
            const TTablet* /*tablet*/,
            const ::google::protobuf::Message& /*message*/) override
        { }
    };

    return New<TDummyMutationForwarder>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
