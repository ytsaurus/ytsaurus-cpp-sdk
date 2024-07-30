#pragma once

#include <yt/yt/library/numeric/fixed_point_number.h>

#include <library/cpp/yt/string/format.h>

#include <yt/yt/core/misc/serialize.h>

#include <yt/yt/core/ytree/serialize.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <typename U, int P>
void Serialize(const TFixedPointNumber<U, P>& number, NYson::IYsonConsumer* consumer)
{
    NYTree::Serialize(static_cast<double>(number), consumer);
}

template <typename U, int P>
void Deserialize(TFixedPointNumber<U, P>& number, NYTree::INodePtr node)
{
    double doubleValue;
    Deserialize(doubleValue, std::move(node));
    number = TFixedPointNumber<U, P>(doubleValue);
}

template <typename U, int P>
void Deserialize(TFixedPointNumber<U, P>& number, NYson::TYsonPullParserCursor* cursor)
{
    auto doubleValue = ExtractTo<double>(cursor);
    number = TFixedPointNumber<U, P>(doubleValue);
}

template <class U, int P>
void FormatValue(TStringBuilderBase* builder, const TFixedPointNumber<U, P>& number, TStringBuf spec)
{
    FormatValue(builder, static_cast<double>(number), spec);
}

////////////////////////////////////////////////////////////////////////////////

struct TFixedPointNumberSerializer
{
    template <class TNumber, class C>
    static void Save(C& context, const TNumber& value)
    {
        NYT::Save(context, value.GetUnderlyingValue());
    }

    template <class TNumber, class C>
    static void Load(C& context, TNumber& value)
    {
        typename std::remove_const<decltype(TNumber::ScalingFactor)>::type underlyingValue;
        NYT::Load(context, underlyingValue);
        value.SetUnderlyingValue(underlyingValue);
    }
};

template <class U, int P, class C>
struct TSerializerTraits<TFixedPointNumber<U, P>, C, void>
{
    using TSerializer = TFixedPointNumberSerializer;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
