#include "chunk_stripe_key.h"

namespace NYT::NChunkPools {

////////////////////////////////////////////////////////////////////////////////

void TBoundaryKeys::Persist(const NTableClient::TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, MinKey);
    Persist(context, MaxKey);
}

bool TBoundaryKeys::operator ==(const TBoundaryKeys& other) const
{
    return MinKey == other.MinKey && MaxKey == other.MaxKey;
}

TBoundaryKeys::operator bool() const
{
    return static_cast<bool>(MinKey) && static_cast<bool>(MaxKey);
}

////////////////////////////////////////////////////////////////////////////////

TChunkStripeKey::TChunkStripeKey(int index)
    : Key_(index)
{ }

TChunkStripeKey::TChunkStripeKey(TBoundaryKeys boundaryKeys)
    : Key_(boundaryKeys)
{ }

TChunkStripeKey::TChunkStripeKey(TOutputOrder::TEntry Entry)
    : Key_(Entry)
{ }

TChunkStripeKey::TChunkStripeKey()
    : Key_(-1)
{ }

bool TChunkStripeKey::IsIndex() const
{
    return std::holds_alternative<int>(Key_);
}

bool TChunkStripeKey::IsBoundaryKeys() const
{
    return std::holds_alternative<TBoundaryKeys>(Key_);
}

bool TChunkStripeKey::IsOutputOrderEntry() const
{
    return std::holds_alternative<TOutputOrder::TEntry>(Key_);
}

TChunkStripeKey::operator bool() const
{
    return !(IsIndex() && AsIndex() == -1);
}

int& TChunkStripeKey::AsIndex()
{
    return std::get<int>(Key_);
}

int TChunkStripeKey::AsIndex() const
{
    return std::get<int>(Key_);
}

TBoundaryKeys& TChunkStripeKey::AsBoundaryKeys()
{
    return std::get<TBoundaryKeys>(Key_);
}

const TBoundaryKeys& TChunkStripeKey::AsBoundaryKeys() const
{
    return std::get<TBoundaryKeys>(Key_);
}

TOutputOrder::TEntry& TChunkStripeKey::AsOutputOrderEntry()
{
    return std::get<TOutputOrder::TEntry>(Key_);
}

const TOutputOrder::TEntry& TChunkStripeKey::AsOutputOrderEntry() const
{
    return std::get<TOutputOrder::TEntry>(Key_);
}

void TChunkStripeKey::Persist(const NTableClient::TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, Key_);
}

bool TChunkStripeKey::operator ==(const TChunkStripeKey& other) const
{
    return Key_ == other.Key_;
}

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TChunkStripeKey& key, TStringBuf /*spec*/)
{
    if (key.IsIndex()) {
        builder->AppendFormat("index@%v", key.AsIndex());
        return;
    }
    if (key.IsBoundaryKeys()) {
        auto boundaryKeys = key.AsBoundaryKeys();
        builder->AppendFormat("bnd_keys@{%v, %v}", boundaryKeys.MinKey, boundaryKeys.MaxKey);
        return;
    }
    if (key.IsOutputOrderEntry()) {
        builder->AppendFormat("%v" ,key.AsOutputOrderEntry());
        return;
    }

    YT_ABORT();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkPools
