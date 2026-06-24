vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Local overlay port. The upstream vcpkg port fetches clipper 6.4.2 from
# SourceForge (sourceforge.net/projects/polyclipping). Behind a filtering proxy
# such as Zscaler, both the SourceForge download mirrors (*.dl.sourceforge.net)
# and the GitHub codeload archive endpoint (codeload.github.com) return HTTP 403,
# so the upstream download fails. The git protocol against github.com is allowed,
# so this overlay fetches the identical clipper 6.4.2 source from a GitHub mirror,
# pinned to an immutable commit. Everything below mirrors the upstream port 1:1;
# only the source-fetch call changed (vcpkg_from_sourceforge -> vcpkg_from_git).
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/bimpp/polyclipping
    REF d912e1b569a44331e6d31cdf3e210febc7cdbcd3
    PATCHES
        fix_targets.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/cpp"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/share/pkgconfig" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
endif()
if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
    file(RENAME "${CURRENT_PACKAGES_DIR}/share/pkgconfig" "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
endif()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/FindCLIPPER.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/clipper")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/clipper")

file(INSTALL "${SOURCE_PATH}/License.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

vcpkg_fixup_pkgconfig()
