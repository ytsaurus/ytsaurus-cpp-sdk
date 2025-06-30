#include "lock_proxy.h"
#include "private.h"
#include "lock.h"
#include "node.h"

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/server/master/object_server/object_detail.h>

#include <yt/yt/server/master/transaction_server/transaction.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NCypressServer {

using namespace NYson;
using namespace NYTree;
using namespace NObjectServer;
using namespace NServer;

////////////////////////////////////////////////////////////////////////////////

class TLockProxy
    : public TNonversionedObjectProxyBase<TLock>
{
public:
    using TNonversionedObjectProxyBase::TNonversionedObjectProxyBase;

private:
    using TBase = TNonversionedObjectProxyBase<TLock>;

    void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        const auto* lock = GetThisImpl();
        const auto& request = lock->Request();

        descriptors->push_back(EInternedAttributeKey::Implicit);
        descriptors->push_back(EInternedAttributeKey::State);
        descriptors->push_back(EInternedAttributeKey::TransactionId);
        descriptors->push_back(EInternedAttributeKey::NodeId);
        descriptors->push_back(EInternedAttributeKey::Mode);
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ChildKey)
            .SetPresent(request.Key.Kind == ELockKeyKind::Child));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::AttributeKey)
            .SetPresent(request.Key.Kind == ELockKeyKind::Attribute));
        descriptors->push_back(EInternedAttributeKey::CreationTime);
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::AcquisitionTime)
            .SetPresent(lock->GetState() == ELockState::Acquired));
    }

    bool GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer) override
    {
        const auto* lock = GetThisImpl();
        const auto& request = lock->Request();

        switch (key) {
            case EInternedAttributeKey::Implicit:
                BuildYsonFluently(consumer)
                    .Value(lock->GetImplicit());
                return true;

            case EInternedAttributeKey::State:
                BuildYsonFluently(consumer)
                    .Value(lock->GetState());
                return true;

            case EInternedAttributeKey::TransactionId:
                BuildYsonFluently(consumer)
                    .Value(GetObjectId(lock->GetTransaction()));
                return true;

            case EInternedAttributeKey::NodeId:
                BuildYsonFluently(consumer)
                    .Value(lock->GetTrunkNode()->GetId());
                return true;

            case EInternedAttributeKey::Mode:
                BuildYsonFluently(consumer)
                    .Value(lock->Request().Mode);
                return true;

            case EInternedAttributeKey::ChildKey:
                if (request.Key.Kind != ELockKeyKind::Child) {
                    break;
                }
                BuildYsonFluently(consumer)
                    .Value(request.Key.Name);
                return true;

            case EInternedAttributeKey::AttributeKey:
                if (request.Key.Kind != ELockKeyKind::Attribute) {
                    break;
                }
                BuildYsonFluently(consumer)
                    .Value(request.Key.Name);
                return true;

            case EInternedAttributeKey::CreationTime:
                BuildYsonFluently(consumer)
                    .Value(lock->GetCreationTime());
                return true;

            case EInternedAttributeKey::AcquisitionTime:
                if (lock->GetState() == ELockState::Acquired) {
                    BuildYsonFluently(consumer)
                        .Value(lock->GetAcquisitionTime());
                    return true;
                }
                break;

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }
};

////////////////////////////////////////////////////////////////////////////////

IObjectProxyPtr CreateLockProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TLock* lock)
{
    return New<TLockProxy>(bootstrap, metadata, lock);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer

