#include "descriptors.h"

#include "schemas.h"

namespace NYT::NPhoenix {

using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

const TString& TFieldDescriptor::GetName() const
{
    return Name_;
}

TFieldTag TFieldDescriptor::GetTag() const
{
    return Tag_;
}

const TFieldSchemaPtr& TFieldDescriptor::GetSchema() const
{
    std::call_once(
        SchemaOnceFlag_,
        [&] {
            Schema_ = New<TFieldSchema>();
            Schema_->Name = Name_;
            Schema_->Tag = Tag_;
        });
    return Schema_;
}

////////////////////////////////////////////////////////////////////////////////

const TString& TTypeDescriptor::GetName() const
{
    return Name_;
}

TTypeTag TTypeDescriptor::GetTag() const
{
    return Tag_;
}

const std::vector<std::unique_ptr<TFieldDescriptor>>& TTypeDescriptor::Fields() const
{
    return Fields_;
}

const std::vector<TTypeTag>& TTypeDescriptor::BaseTypeTags() const
{
    return BaseTypeTags_;
}

bool TTypeDescriptor::IsTemplate() const
{
    return Template_;
}

const TTypeSchemaPtr& TTypeDescriptor::GetSchema() const
{
   std::call_once(
        SchemaOnceFlag_,
        [&] {
            Schema_ = New<TTypeSchema>();
            Schema_->Name = Name_;
            Schema_->Tag = Tag_;
            for (const auto& fieldDescriptor : Fields_) {
                Schema_->Fields.push_back(fieldDescriptor->GetSchema());
            }
            Schema_->BaseTypeTags = BaseTypeTags_;
            Schema_->Template = Template_;
        });
    return Schema_;
}

const TYsonString& TTypeDescriptor::GetSchemaYson() const
{
   std::call_once(
        SchemaYsonOnceFlag_,
        [&] {
            SchemaYson_ = ConvertToYsonString(GetSchema());
        });
    return SchemaYson_;
}

////////////////////////////////////////////////////////////////////////////////

const TUniverseSchemaPtr& TUniverseDescriptor::GetSchema() const
{
    std::call_once(
        SchemaOnceFlag_,
        [&] {
            Schema_ = New<TUniverseSchema>();
            for (const auto& [typeTag, typeDescriptor] : TypeTagToDescriptor_) {
                Schema_->Types.push_back(typeDescriptor->GetSchema());
            }
        });
    return Schema_;
}

const TYsonString& TUniverseDescriptor::GetSchemaYson() const
{
    std::call_once(
        SchemaYsonOnceFlag_,
        [&] {
            SchemaYson_ = ConvertToYsonString(GetSchema());
        });
    return SchemaYson_;
}

const TTypeDescriptor* TUniverseDescriptor::FindTypeDescriptorByTag(TTypeTag tag) const
{
    auto it = TypeTagToDescriptor_.find(tag);
    return it == TypeTagToDescriptor_.end() ? nullptr : it->second.get();
}

const TTypeDescriptor& TUniverseDescriptor::GetTypeDescriptorByTagOrThrow(TTypeTag tag) const
{
    const auto* descriptor = FindTypeDescriptorByTag(tag);
    if (!descriptor) {
        THROW_ERROR_EXCEPTION("Type @%x is not registered", tag);
    }
    return *descriptor;
}

const TTypeDescriptor& TUniverseDescriptor::GetTypeDescriptorByTag(TTypeTag tag) const
{
    const auto* descriptor = FindTypeDescriptorByTag(tag);
    YT_VERIFY(descriptor);
    return *descriptor;
}

const TTypeDescriptor* TUniverseDescriptor::FindTypeDescriptorByTypeIndex(std::type_index typeIndex) const
{
    auto it = TypeIndexToDescriptor_.find(typeIndex);
    return it == TypeIndexToDescriptor_.end() ? nullptr : it->second;
}

const TTypeDescriptor& TUniverseDescriptor::GetTypeDescriptorByTypeIndex(std::type_index typeIndex) const
{
    const auto* descriptor = FindTypeDescriptorByTypeIndex(typeIndex);
    YT_VERIFY(descriptor);
    return *descriptor;
}

const TTypeDescriptor& TUniverseDescriptor::GetTypeDescriptorByTypeIndexOrThrow(std::type_index typeIndex) const
{
    const auto* descriptor = FindTypeDescriptorByTypeIndex(typeIndex);
    if (!descriptor) {
        THROW_ERROR_EXCEPTION("Type %v is not registered",
            CppDemangle(typeIndex.name()));
    }
    return *descriptor;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NPhoenix

