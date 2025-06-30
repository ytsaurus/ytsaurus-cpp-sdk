#include "phdr_cache.h"

#include <util/system/sanitizers.h>

#if defined(__linux__) && !defined(_tsan_enabled_)

#include <link.h>
#include <dlfcn.h>
#include <assert.h>
#include <vector>
#include <cstddef>

#include <library/cpp/yt/assert/assert.h>

namespace NYT::NPhdrCache {

////////////////////////////////////////////////////////////////////////////////

// This is adapted from
// https://github.com/scylladb/seastar/blob/master/core/exception_hacks.hh
// https://github.com/scylladb/seastar/blob/master/core/exception_hacks.cc

using TDlIterateFunc = int (*) (int (*callback) (struct dl_phdr_info* info, size_t size, void* data), void* data);

TDlIterateFunc GetOriginalDlIteratePhdr()
{
    static auto result = [] {
        auto func = reinterpret_cast<TDlIterateFunc>(dlsym(RTLD_NEXT, "dl_iterate_phdr"));
        YT_VERIFY(func);
        return func;
    }();
    return result;
}

// Never destroyed to avoid races with static destructors.
std::vector<dl_phdr_info>* PhdrCache;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NPhdrCache

extern "C" Y_WEAK
#ifndef __clang__
[[gnu::visibility("default")]]
[[gnu::externally_visible]]
#endif
int dl_iterate_phdr(int (*callback) (struct dl_phdr_info* info, size_t size, void* data), void* data)
{
    using namespace NYT::NPhdrCache;
    if (!PhdrCache) {
        // Cache is not yet populated, pass through to the original function.
        return GetOriginalDlIteratePhdr()(callback, data);
    }
    int result = 0;
    for (auto& info : *PhdrCache) {
        result = callback(&info, offsetof(dl_phdr_info, dlpi_adds), data);
        if (result != 0) {
            break;
        }
    }
    return result;
}

#endif

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

void EnablePhdrCache()
{
#if defined(__linux__) && !defined(_tsan_enabled_)
    using namespace NPhdrCache;
    if (PhdrCache) {
        return;
    }
    // Fill out ELF header cache for access without locking.
    // This assumes no dynamic object loading/unloading after this point
    PhdrCache = new std::vector<dl_phdr_info>();
    GetOriginalDlIteratePhdr()(
        [] (struct dl_phdr_info *info, size_t /*size*/, void* /*data*/) {
            PhdrCache->push_back(*info);
            return 0;
        },
        nullptr);
#endif
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
