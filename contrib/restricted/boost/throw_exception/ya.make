# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(BSL-1.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.86.0)

ORIGINAL_SOURCE(https://github.com/boostorg/throw_exception/archive/boost-1.86.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/assert
    contrib/restricted/boost/config
)

ADDINCL(
    GLOBAL contrib/restricted/boost/throw_exception/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

END()