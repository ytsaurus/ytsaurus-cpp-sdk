#pragma once

#include <yt/yt/library/codegen/type_builder.h>

#include <yt/yt/library/query/engine_api/position_independent_value.h>
#include <yt/yt/library/query/engine_api/public.h>

#include <type_traits>

namespace NYT::NCodegen {

using llvm::Type;
using llvm::StructType;
using llvm::LLVMContext;
using llvm::ArrayRef;

////////////////////////////////////////////////////////////////////////////////

template <>
struct TTypeBuilder<NYT::NQueryClient::TPositionIndependentValueData>
{
public:
    using TBoolean = TTypeBuilder<char>;
    using TInt64 = TTypeBuilder<i64>;
    using TUint64 = TTypeBuilder<ui64>;
    using TDouble = TTypeBuilder<double>;
    using TStringType = TTypeBuilder<const char*>;
    using TAny = TTypeBuilder<const char*>;
    using TComposite = TTypeBuilder<const char*>;
    using TPointerDiff = TTypeBuilder<ptrdiff_t>;

    static Type* Get(LLVMContext& context)
    {
        return TTypeBuilder<i64>::Get(context);
    }

    static Type* Get(LLVMContext& context, NYT::NTableClient::EValueType staticType)
    {
        using NYT::NTableClient::EValueType;
        switch (staticType) {
            case EValueType::Boolean:
                return TBoolean::Get(context);
            case EValueType::Int64:
                return TInt64::Get(context);
            case EValueType::Uint64:
                return TUint64::Get(context);
            case EValueType::Double:
                return TDouble::Get(context);
            case EValueType::String:
                return TStringType::Get(context);
            case EValueType::Any:
                return TAny::Get(context);
            case EValueType::Composite:
                return TComposite::Get(context);

            case EValueType::Null:

            case EValueType::Min:
            case EValueType::Max:
            case EValueType::TheBottom:
                break;
        }
        YT_ABORT();
    }

    static_assert(
        std::is_union<NYT::NQueryClient::TPositionIndependentValueData>::value,
        "TPositionIndependentValueData must be a union");
    static_assert(
        sizeof(NYT::NQueryClient::TPositionIndependentValueData) == 8,
        "TPositionIndependentValueData size must be 64bit");
};

template <>
struct TTypeBuilder<NYT::NQueryClient::TPositionIndependentValue>
{
public:
    using TId = TTypeBuilder<ui16>;
    using TType = TTypeBuilder<ui8>;
    using TAggregate = TTypeBuilder<ui8>;
    using TLength = TTypeBuilder<ui32>;
    using TData = TTypeBuilder<NYT::NQueryClient::TPositionIndependentValueData>;

    enum Fields
    {
        Id,
        Type,
        Aggregate,
        Length,
        Data
    };

    static StructType* Get(LLVMContext& context)
    {
        return StructType::get(context, {
            TId::Get(context),
            TType::Get(context),
            TAggregate::Get(context),
            TLength::Get(context),
            TData::Get(context)});
    }

    static_assert(
        std::is_standard_layout<NYT::NQueryClient::TPositionIndependentValue>::value);
    static_assert(
        sizeof(NYT::NQueryClient::TPositionIndependentValue) == 16);
    static_assert(
        offsetof(NYT::NQueryClient::TPositionIndependentValue, Id) == 0 &&
        sizeof(NYT::NQueryClient::TPositionIndependentValue::Id) == 2);
    static_assert(
        offsetof(NYT::NQueryClient::TPositionIndependentValue, Type) == 2 &&
        sizeof(NYT::NQueryClient::TPositionIndependentValue::Type) == 1);
    static_assert(
        offsetof(NYT::NQueryClient::TPositionIndependentValue, Flags) == 3 &&
        sizeof(NYT::NQueryClient::TPositionIndependentValue::Flags) == 1);
    static_assert(
        offsetof(NYT::NQueryClient::TPositionIndependentValue, Length) == 4 &&
        sizeof(NYT::NQueryClient::TPositionIndependentValue::Length) == 4);
    static_assert(
        offsetof(NYT::NQueryClient::TPositionIndependentValue, Data) == 8 &&
        sizeof(NYT::NQueryClient::TPositionIndependentValue::Data) == 8);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCodegen
