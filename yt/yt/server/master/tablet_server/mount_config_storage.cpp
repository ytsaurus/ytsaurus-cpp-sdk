#include "mount_config_storage.h"

#include "private.h"

#include <yt/yt/server/master/cell_master/serialize.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/serialize.h>

namespace NYT::NTabletServer {

using namespace NYson;
using namespace NYTree;
using namespace NTabletNode;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

void DropUnrecognizedRecursively(const IMapNodePtr& node, const IMapNodePtr& unrecognized)
{
    for (const auto& key : unrecognized->GetKeys()) {
        if (node->FindChild(key)) {
            node->RemoveChild(key);
        }
    }

    for (const auto& [key, child] : node->GetChildren()) {
        if (child->GetType() != ENodeType::Map) {
            continue;
        }

        if (auto unrecognizedChild = unrecognized->FindChild(key);
            unrecognizedChild && unrecognizedChild->GetType() == ENodeType::Map)
        {
            DropUnrecognizedRecursively(child->AsMap(), unrecognizedChild->AsMap());
        }
    }
}

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

void TMountConfigStorage::Set(TStringBuf key, const TYsonString& value)
{
    ValidateKey(key);

    TAttributeSet::Set(key, value);
}

bool TMountConfigStorage::TryRemove(TStringBuf key)
{
    // TODO: add validation specific for mount config.
    return TAttributeSet::TryRemove(key);
}

void TMountConfigStorage::SetSelf(const NYson::TYsonString& value)
{
    auto node = ConvertToNode(value);
    auto mapNode = node->AsMap();

    for (const auto& key : mapNode->GetKeys()) {
        ValidateKey(key);
    }

    Deserialize(*this, node);
}

void TMountConfigStorage::Clear()
{
    Attributes_.clear();
    MasterMemoryUsage_ = 0;
}

bool TMountConfigStorage::IsEmpty() const
{
    return Attributes_.empty();
}

TCustomTableMountConfigPtr TMountConfigStorage::GetEffectiveConfig() const
{
    return ConvertTo<TCustomTableMountConfigPtr>(Attributes_);
}

std::pair<IMapNodePtr, IMapNodePtr> TMountConfigStorage::GetRecognizedConfig() const
{
    auto providedConfig = GetEphemeralNodeFactory()->CreateMap();
    for (const auto& [key, value] : Attributes_) {
        providedConfig->AddChild(key, ConvertToNode(value));
    }

    auto effectiveConfig = New<TCustomTableMountConfig>();
    effectiveConfig->SetUnrecognizedStrategy(EUnrecognizedStrategy::KeepRecursive);
    effectiveConfig->Load(providedConfig);

    auto unrecognizedConfig = effectiveConfig->GetRecursiveUnrecognized();

    NDetail::DropUnrecognizedRecursively(providedConfig, unrecognizedConfig);

    return {std::move(providedConfig), std::move(unrecognizedConfig)};
}

void TMountConfigStorage::ValidateKey(TStringBuf key) const
{
    static const THashSet<std::string, THash<TStringBuf>, TEqualTo<>> forbiddenKeySet(
        TBuiltinTableMountConfig::NonDynamicallyModifiableFields.begin(),
        TBuiltinTableMountConfig::NonDynamicallyModifiableFields.end());

    if (forbiddenKeySet.contains(key)) {
        THROW_ERROR_EXCEPTION("Field %Qv cannot be set to \"mount_config\" attribute, "
            "it should be set at root as \"@%v\"",
            key,
            key);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TMountConfigStorage& storage, IYsonConsumer* consumer)
{
    NYTree::Serialize(storage.Attributes(), consumer);
}

void Deserialize(TMountConfigStorage& storage, NYTree::INodePtr node)
{
    storage.Clear();

    auto mapNode = node->AsMap();
    for (const auto& [key, child] : mapNode->GetChildren()) {
        storage.Set(key, ConvertToYsonString(child));
    }
}

void Deserialize(TMountConfigStorage& storage, NYson::TYsonPullParserCursor* cursor)
{
    Deserialize(storage, ExtractTo<INodePtr>(cursor));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
