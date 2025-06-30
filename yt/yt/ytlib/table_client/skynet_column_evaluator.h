#pragma once

#include "public.h"

#include <yt/yt/client/table_client/unversioned_row.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

void ValidateSkynetSchema(const TTableSchema& schema);

class TSkynetHashState;

class TSkynetColumnEvaluator
    : public TRefCounted
{
public:
    explicit TSkynetColumnEvaluator(const TTableSchema& schema);

    ~TSkynetColumnEvaluator();

    //! Expects row to has "filename", "part_index" and "data" fields.
    //! Fills "sha1", "md5" and "data_size" fields.
    void ValidateAndComputeHashes(
        TMutableUnversionedRow fullRow,
        const TRowBufferPtr& buffer,
        bool isLastRow);

private:
    const int FilenameId_;
    const int PartIndexId_;
    const int DataId_;

    const int Sha1Id_;
    const int Md5Id_;
    const int DataSizeId_;

    int EffectiveKeySize_;

    std::optional<TString> LastFileName_;
    i64 LastDataSize_ = 0;
    i64 NextPartIndex_ = 0;

    TUnversionedRow LastKey_;
    TLegacyOwningKey LastKeyHolder_;

    std::unique_ptr<TSkynetHashState> HashState_;

    void UnpackFields(
        TMutableUnversionedRow fullRow,
        TStringBuf* filename,
        TStringBuf* data,
        i64* partIndex,
        TUnversionedValue** Sha1,
        TUnversionedValue** Md5,
        TUnversionedValue** DataSize);

    bool IsKeySwitched(TUnversionedRow fullRow, bool isLastRow);
};

DEFINE_REFCOUNTED_TYPE(TSkynetColumnEvaluator)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
