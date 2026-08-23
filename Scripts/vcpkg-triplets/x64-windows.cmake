# Stock x64-windows plus an explicitly pinned MSVC toolset.
#
# vcpkg and CMake select a toolset by different rules: vcpkg takes the latest installed minor
# version, a bare "-T v143" takes the VS instance default. When those diverge the engine compiles
# with one toolset and links libs built by another, and Catch2 fails with LNK2019 on vectorized-STL
# symbols (__std_search_1, __std_find_last_of_trivial_pos_1). Generate-Solution.py resolves one
# version and gives it to both sides, so they agree by construction.
#
# Overriding the stock triplet NAME keeps "--triplet x64-windows" working unchanged everywhere.
# The version arrives through the environment because a triplet is static but the value is
# per-machine; empty falls back to vcpkg's own default, which is correct when detection failed or
# vcpkg is invoked directly.

# Keep in sync with vcpkg/triplets/x64-windows.cmake.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

if(NOT "$ENV{SS_MSVC_TOOLSET_VERSION}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET v143)
    set(VCPKG_PLATFORM_TOOLSET_VERSION "$ENV{SS_MSVC_TOOLSET_VERSION}")
endif()
