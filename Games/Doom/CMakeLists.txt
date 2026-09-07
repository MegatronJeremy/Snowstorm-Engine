# Doom: a GAME built on Snowstorm, not a part of it.
#
# The engine knows nothing about this target. It consumes Snowstorm-Core the way any game would, and
# offers hosts exactly one entry point, RegisterDoomSystems (DoomGame.hpp). The Editor and the Runtime
# link it so Doom.world can be authored and played in either; Snowstorm-Doom is the shipping game.
#
# Built UNCONDITIONALLY, while only the GPL doomgeneric link is gated on SS_ENABLE_DOOM. That is
# deliberate: DoomComponent's registration is what lets SceneSerializer round-trip Doom.world, and the
# serializer drops an unregistered component silently (a bare `continue`), so gating the whole target
# would make a stock editor delete the DoomComponent block from the scene on the next save. Without
# SS_ENABLE_DOOM nothing GPL is fetched or linked and every TU here is inert behind SS_HAS_DOOM, so
# the licence property is exactly what it was.
cmake_minimum_required(VERSION 3.15)
project(DoomGame VERSION 1.0 LANGUAGES CXX)

# Only *.cpp. OplDriver.c is deliberately NOT built here: it needs Chocolate Doom's opl/ headers, which
# are PUBLIC on the doomgeneric target, so that target compiles it (see Vendor/doomgeneric/CMakeLists).
file(GLOB_RECURSE DOOM_GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)

add_library(DoomGame STATIC ${DOOM_GAME_SOURCES})

set_target_properties(DoomGame PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    FOLDER Games
)

target_include_directories(DoomGame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# PUBLIC so a host linking DoomGame also gets Core's headers and, when Doom is on, SS_HAS_DOOM.
target_link_libraries(DoomGame PUBLIC Snowstorm-Core)

if(SS_ENABLE_DOOM)
    # Carries SS_HAS_DOOM (PUBLIC on that target), which is what makes every TU here non-inert.
    target_link_libraries(DoomGame PUBLIC doomgeneric)
endif()

# No precompiled header here on purpose. Core's pch force-includes Windows.h, whose rpcndr.h typedefs
# `boolean` as unsigned char while doomtype.h typedefs it as unsigned int; Core needed a per-file
# SKIP_PRECOMPILE_HEADERS to work around that, and a target with no pch simply does not have the problem.
