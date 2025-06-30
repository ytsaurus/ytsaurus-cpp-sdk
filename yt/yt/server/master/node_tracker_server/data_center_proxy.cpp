#include "data_center_proxy.h"
#include "data_center.h"
#include "node.h"
#include "node_tracker.h"
#include "rack.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/server/master/object_server/object_detail.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NNodeTrackerServer {

using namespace NYTree;
using namespace NYson;
using namespace NObjectServer;
using namespace NServer;

////////////////////////////////////////////////////////////////////////////////

class TDataCenterProxy
    : public TNonversionedObjectProxyBase<TDataCenter>
{
public:
    using TNonversionedObjectProxyBase::TNonversionedObjectProxyBase;

private:
    using TBase = TNonversionedObjectProxyBase<TDataCenter>;

    void ValidateRemoval() override
    {
        ValidatePermission(EPermissionCheckScope::This, EPermission::Remove);
    }

    void ListSystemAttributes(std::vector<ISystemAttributeProvider::TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Name)
            .SetWritable(true)
            .SetReplicated(true)
            .SetMandatory(true));
        descriptors->push_back(EInternedAttributeKey::Racks);
    }

    bool GetBuiltinAttribute(TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override
    {
        auto nodeTracker = Bootstrap_->GetNodeTracker();
        const auto* dc = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::Name:
                BuildYsonFluently(consumer)
                    .Value(dc->GetName());
                return true;

            case EInternedAttributeKey::Racks: {
                auto racks = nodeTracker->GetDataCenterRacks(dc);
                BuildYsonFluently(consumer)
                    .DoListFor(racks, [] (TFluentList fluent, const TRack* rack) {
                        fluent.Item().Value(rack->GetName());
                    });
                return true;
            }

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

    bool SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value, bool force) override
    {
        auto* dc = GetThisImpl();
        auto nodeTracker = Bootstrap_->GetNodeTracker();

        switch (key) {
            case EInternedAttributeKey::Name: {
                auto newName = ConvertTo<std::string>(value);
                nodeTracker->RenameDataCenter(dc, newName);
                return true;
            }

            default:
                break;
        }

        return TBase::SetBuiltinAttribute(key, value, force);
    }
};

////////////////////////////////////////////////////////////////////////////////

IObjectProxyPtr CreateDataCenterProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TDataCenter* dc)
{
    return New<TDataCenterProxy>(bootstrap, metadata, dc);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
