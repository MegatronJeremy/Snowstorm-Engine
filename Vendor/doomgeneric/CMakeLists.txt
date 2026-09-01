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
)
list(TRANSFORM DOOMGENERIC_SOURCES PREPEND "${doomgeneric_SOURCE_DIR}/doomgeneric/")

add_library(doomgeneric STATIC ${DOOMGENERIC_SOURCES})

set_target_properties(doomgeneric PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    FOLDER Vendor
)

target_include_directories(doomgeneric PUBLIC "${doomgeneric_SOURCE_DIR}/doomgeneric")

if(MSVC)
    # 1993 C built by a 2022 compiler: the warnings are upstream's to fix, and burying our own under
    # thousands of theirs is worse than not seeing theirs. Excluded from clang-format for the same reason.
    target_compile_options(doomgeneric PRIVATE /W0)
    target_compile_definitions(doomgeneric PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Marks the availability of the DG_* entry points for the engine-side glue, which is compiled
# unconditionally and inert without it.
target_compile_definitions(doomgeneric PUBLIC SS_HAS_DOOM)
