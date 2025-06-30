#pragma once

#include <llvm/ADT/FoldingSet.h>

#include <util/str_stl.h>

////////////////////////////////////////////////////////////////////////////////

// A hasher for llvm::FoldingSetNodeID
template <>
struct THash<llvm::FoldingSetNodeID>
{
    inline size_t operator()(const llvm::FoldingSetNodeID& id) const
    {
        return id.ComputeHash();
    }
};

////////////////////////////////////////////////////////////////////////////////
