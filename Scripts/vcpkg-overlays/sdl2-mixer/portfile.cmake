# Overlay of the upstream sdl2-mixer port, identical except that MIDI uses the platform's own
# synthesiser. Upstream hardcodes native MIDI off and only turns MIDI on at all for the
# fluidsynth/timidity features, both of which need an external soundfont or GUS patch set at runtime.
# Doom's music is MUS converted to MIDI (mus2mid), so without a MIDI backend the embedded Doom
# (SS_ENABLE_DOOM) renders sound effects and silence. Native MIDI plays through the Windows synth with
# no data files to ship.
#
# Re-sync this with vcpkg/ports/sdl2-mixer when the submodule's vcpkg is updated: it is a copy, not a
# patch, so it does not track upstream on its own.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libsdl-org/SDL_mixer
    REF "release-${VERSION}"
    SHA512 4c2ba587a89721e060472b65e8a846ed3012121b4de7a2952704dab5df5f9e5d828a4105a7eb9b2fd65158ea9264e8b53eb689b87cb7f098452c2ab959a25a06
    PATCHES 
        fix-pkg-prefix.patch
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        fluidsynth SDL2MIXER_MIDI_FLUIDSYNTH
        libflac SDL2MIXER_FLAC
        libflac SDL2MIXER_FLAC_LIBFLAC
        libmodplug SDL2MIXER_MOD
        libmodplug SDL2MIXER_MOD_MODPLUG
        mpg123 SDL2MIXER_MP3
        mpg123 SDL2MIXER_MP3_MPG123
        timidity SDL2MIXER_MIDI_TIMIDITY
        wavpack SDL2MIXER_WAVPACK
        wavpack SDL2MIXER_WAVPACK_DSD
        opusfile SDL2MIXER_OPUS
)

# Native MIDI needs no dependency, so MIDI is unconditionally available here rather than only with
# fluidsynth/timidity (the upstream condition).
list(APPEND FEATURE_OPTIONS "-DSDL2MIXER_MIDI=ON")

if("fluidsynth" IN_LIST FEATURES)
    vcpkg_find_acquire_program(PKGCONFIG)
    list(APPEND EXTRA_OPTIONS "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}")
endif()

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" BUILD_SHARED)

# Native MIDI exists on Windows (winmm) and macOS; elsewhere there is no platform synth to use.
if(VCPKG_TARGET_IS_WINDOWS OR VCPKG_TARGET_IS_OSX)
    set(MIDI_NATIVE ON)
else()
    set(MIDI_NATIVE OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        ${EXTRA_OPTIONS}
        -DSDL2MIXER_VENDORED=OFF
        -DSDL2MIXER_SAMPLES=OFF
        -DSDL2MIXER_DEPS_SHARED=OFF
        -DSDL2MIXER_OPUS_SHARED=OFF
        -DSDL2MIXER_VORBIS_VORBISFILE_SHARED=OFF
        -DSDL2MIXER_VORBIS="VORBISFILE"
        -DSDL2MIXER_FLAC_DRFLAC=OFF
        -DSDL2MIXER_MIDI_NATIVE=${MIDI_NATIVE}
        -DSDL2MIXER_MP3_DRMP3=OFF
        -DSDL2MIXER_MOD_XMP_SHARED=${BUILD_SHARED}
    MAYBE_UNUSED_VARIABLES
        SDL2MIXER_MP3_DRMP3
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(
    PACKAGE_NAME "SDL2_mixer"
    CONFIG_PATH "lib/cmake/SDL2_mixer"
)
vcpkg_fixup_pkgconfig()

set(debug_libname "SDL2_mixerd")
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static" AND VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/SDL2_mixer.pc" "-lSDL2_mixer" "-lSDL2_mixer-static")
    set(debug_libname "SDL2_mixer-staticd")
endif()

if(NOT VCPKG_BUILD_TYPE)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/SDL2_mixer.pc" "-lSDL2_mixer" "-l${debug_libname}")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
