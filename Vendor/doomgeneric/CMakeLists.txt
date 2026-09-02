# doomgeneric (https://github.com/ozkl/doomgeneric): Doom reduced to an embeddable interpreter, driven
# by doomgeneric_Create/doomgeneric_Tick with the whole platform layer behind six DG_* callbacks. The
# engine implements those in Snowstorm-Editor/Source/Examples/Doom.
#
# Fetched at configure time rather than checked in, deliberately. doomgeneric is GPL-2.0 (inherited
# from id Software's 1999 relicense of the Doom source) and Snowstorm is public domain, so committing
# it here would put GPL code in this repository's history permanently. Fetching into the build tree
# keeps the two licenses in separate places, at the cost of needing network access on first configure.
# A binary built with SS_ENABLE_DOOM=ON is a combined work under GPL-2.0 and cannot be redistributed
# under UNLICENSE.txt.

include(FetchContent)

# Pinned to a commit: upstream cuts no releases, so a floating master would silently change what this
# builds. Same reasoning as the clang-format and RGA version pins.
FetchContent_Declare(doomgeneric
    GIT_REPOSITORY https://github.com/ozkl/doomgeneric.git
    GIT_TAG dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284
)

# Populate only. Upstream ships Makefiles and a .vcxproj, no CMakeLists, so MakeAvailable defines
# doomgeneric_SOURCE_DIR without adding a subdirectory.
FetchContent_MakeAvailable(doomgeneric)

# Chocolate Doom, for the OPL music path only. doomgeneric stripped it: i_sound.h still declares
# music_opl_module but ships no implementation, no opl/ directory and no midifile.c.
#
# Pinned to the 2.1.0 TAG rather than master on purpose. That is doomgeneric's fork point, so
# i_sound.h matches and music_module_t needs no adaptation; master has since changed opl.h's API and
# swapped the synth for Nuked-OPL3, which would be a port rather than a drop-in. 2.1.0's dbopl.c (the
# DOSBox core) is also markedly cheaper to run, which matters when it renders inside a mixer callback.
#
# Also GPL-2.0, so fetched rather than vendored for exactly the reason above.
FetchContent_Declare(chocolatedoom
    GIT_REPOSITORY https://github.com/chocolate-doom/chocolate-doom.git
    GIT_TAG chocolate-doom-2.1.0
)
FetchContent_MakeAvailable(chocolatedoom)

# SRC_DOOM from upstream's Makefile, minus doomgeneric_xlib.c: that is the X11 backend's copy of the
# DG_* callbacks plus its main(). Every other doomgeneric_<platform>.c is a sibling of it. Taking
# upstream's own manifest rather than globbing keeps this honest about which files are actually part
# of the interpreter, and a glob would sweep all six backends back in.
set(DOOMGENERIC_SOURCES
    am_map.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c
    doomdef.c doomgeneric.c doomstat.c dstrings.c dummy.c
    f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c
    i_cdmus.c i_endoom.c i_input.c i_joystick.c i_scale.c i_sound.c i_system.c i_timer.c i_video.c
    info.c memio.c
    m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c
    p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c
    r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c
    sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c
    w_checksum.c w_file.c w_file_stdc.c w_main.c w_wad.c z_zone.c
    # mus2mid.c converts Doom's MUS lumps to the MIDI the OPL sequencer plays. Both SDL backends are
    # deliberately ABSENT: DG_sound_module and DG_music_module are now provided by the engine
    # (Examples/Doom), so all of Doom's audio goes through AudioService and one output device.
    # Dead code without FEATURE_SOUND, which is why it is not in the base list.
    mus2mid.c
)
# The OPL music path, from Chocolate Doom. Deliberately NOT opl.c: it reaches SDL (OPL_Delay waits on
# an SDL condvar) and builds a driver table that drags in the Win32 hardware-port backend. The engine
# provides those entry points itself instead, so only the synth, the callback queue and the MIDI parser
# come from upstream unmodified.
set(CHOCOLATE_OPL_SOURCES
    opl/dbopl.c
    opl/opl_queue.c
    src/midifile.c
    src/i_oplmusic.c
)
list(TRANSFORM CHOCOLATE_OPL_SOURCES PREPEND "${chocolatedoom_SOURCE_DIR}/")

list(TRANSFORM DOOMGENERIC_SOURCES PREPEND "${doomgeneric_SOURCE_DIR}/doomgeneric/")

# The replacement for opl_sdl.c. It lives with its siblings in the engine tree but compiles HERE,
# because it is the one engine file that needs Chocolate Doom's opl/ headers, and because Core globs
# only *.cpp. Its C++ half (threads, mutexes, AudioService) is DoomAudioBridge.cpp, in Core.
set(SS_OPL_DRIVER "${CMAKE_SOURCE_DIR}/Snowstorm-Core/Source/Snowstorm/Examples/Doom/OplDriver.c")

add_library(doomgeneric STATIC ${DOOMGENERIC_SOURCES} ${CHOCOLATE_OPL_SOURCES} ${SS_OPL_DRIVER})

set_target_properties(doomgeneric PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    FOLDER Vendor
)

target_include_directories(doomgeneric PUBLIC
    "${doomgeneric_SOURCE_DIR}/doomgeneric"
    "${chocolatedoom_SOURCE_DIR}/opl"
)
# PRIVATE: only the OPL sources need Chocolate Doom's src/, and putting it on the PUBLIC path would
# let its headers shadow doomgeneric's identically-named forks for every other consumer.
target_include_directories(doomgeneric PRIVATE "${chocolatedoom_SOURCE_DIR}/src")

# Two small shims that let the SDL libraries go away entirely: Chocolate Doom's i_swap.h and midifile.c
# include SDL_endian.h for byte swaps (the big-endian pair is real work, since MIDI headers are
# big-endian on disk), and doomgeneric's i_sound.c has a vestigial SDL_mixer.h include that nothing
# actually calls. See the headers.
target_include_directories(doomgeneric PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")

if(MSVC)
    # 1993 C built by a 2022 compiler: the warnings are upstream's to fix, and burying our own under
    # thousands of theirs is worse than not seeing theirs. Excluded from clang-format for the same reason.
    target_compile_options(doomgeneric PRIVATE /W0)
    target_compile_definitions(doomgeneric PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Audio. FEATURE_SOUND is what compiles the sound/music module tables in i_sound.c at all; without it
# doomgeneric references neither DG_sound_module nor DG_music_module and plays silence.
#
# Music is OPL FM synthesis (Chocolate Doom's sequencer + dbopl, driven by Examples/Doom/OplDriver.c),
# not MIDI. That is what lets it go through the engine's mixer at all: miniaudio has no MIDI
# synthesiser, General MIDI would need a soundfont this repo cannot ship, and OPL's instrument bank
# (GENMIDI) already lives inside the IWAD. SDL2 and SDL2_mixer are no longer needed by anything.
target_compile_definitions(doomgeneric PRIVATE FEATURE_SOUND)

# doomgeneric's i_sound.c hardcodes music_module = &DG_music_module, while Chocolate Doom's file
# defines music_opl_module. Renaming at compile time satisfies both without patching GPL source in a
# fetched tree. It also resolves doomgeneric's dangling opl_io_port extern, which m_config.c binds as a
# config variable and i_oplmusic.c is the file that actually defines.
set_source_files_properties("${chocolatedoom_SOURCE_DIR}/src/i_oplmusic.c" PROPERTIES
    COMPILE_DEFINITIONS "music_opl_module=DG_music_module"
)

# Marks the availability of the DG_* entry points for the engine-side glue, which is compiled
# unconditionally and inert without it.
target_compile_definitions(doomgeneric PUBLIC SS_HAS_DOOM)
