cmake_minimum_required(VERSION 3.16)
project(Snowstorm LANGUAGES CXX)

# Where THIS engine lives, which is not CMAKE_SOURCE_DIR whenever Snowstorm is consumed as a
# subdirectory by a game in its own repository: there CMAKE_SOURCE_DIR is the GAME's root, and every
# path built from it points at files that do not exist. Use this for anything inside the engine.
set(SS_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Snowstorm engine source root")

# Global settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# MSVC compiles the TUs of a single target serially by default; /MP fans them across cores. Our
# targets glob dozens-to-hundreds of .cpp, so this is the single biggest clean-build speedup. Applied
# globally (all targets) and only for MSVC. Combined with the precompiled header wired on Snowstorm-Core.
if(MSVC)
    add_compile_options(/MP)
endif()

add_subdirectory(Snowstorm-Core)

# Games built on the engine. Not part of it: the engine has no knowledge of anything under Games/, and
# a host opts in by linking the game and calling its one registration function.
add_subdirectory(Games/Pong)
add_subdirectory(Snowstorm-Editor)
add_subdirectory(Snowstorm-Runtime)

enable_testing()
add_subdirectory(Snowstorm-Tests)

# 2. Set the default startup project for Visual Studio/Rider
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "Snowstorm-Editor")

# 3. Set working directory for the executable targets
set_property(TARGET Snowstorm-Editor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(TARGET Snowstorm-Runtime PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

set(VCPKG_LAYER_PATH "${CMAKE_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/bin")

# Set Vulkan validation layer path environment variable
set(VK_ENV "VK_ADD_LAYER_PATH=${VCPKG_LAYER_PATH}")

set_target_properties(Snowstorm-Editor PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VK_ENV}")
set_target_properties(Snowstorm-Runtime PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VK_ENV}")

# The VS_DEBUGGER_ENVIRONMENT above is Visual-Studio-only: Rider / a bare exe / the terminal don't get it,
# so validation silently fails to load there ("layers requested but not available"). Also bake the absolute
# layer path in as a Debug-only compile definition so EntryPoint can set VK_ADD_LAYER_PATH itself before
# Vulkan init — making validation work regardless of how the app is launched. Debug only (validation is a
# dev tool). Backslashes in the Windows path would break the C string literal, so normalize to forward
# slashes (the Vulkan loader accepts them).
string(REPLACE "\\" "/" VCPKG_LAYER_PATH_FWD "${VCPKG_LAYER_PATH}")
target_compile_definitions(Snowstorm-Core PUBLIC $<$<CONFIG:Debug>:SS_VULKAN_LAYER_PATH="${VCPKG_LAYER_PATH_FWD}">)

# 4. Browsable Shaders target: surfaces Engine/Shaders/*.hlsl(i) in the IDE so they can be viewed and
# edited alongside the C++, instead of only reachable by opening the files externally. IDE-only
# container (add_custom_target ... SOURCES) -- nothing is compiled or linked here; shaders are still
# compiled at runtime by DXC (hash-cached). Attached to no binary because they're shared engine assets.
file(GLOB SHADER_FILES
    "${SS_ENGINE_SOURCE_DIR}/Engine/Shaders/*.hlsl"
    "${SS_ENGINE_SOURCE_DIR}/Engine/Shaders/Include/*.hlsli"
)
add_custom_target(Shaders SOURCES ${SHADER_FILES})
# Mirror the on-disk layout in the IDE tree (Shaders/ + Shaders/Include/).
source_group(TREE "${SS_ENGINE_SOURCE_DIR}/Engine/Shaders" PREFIX "Shaders" FILES ${SHADER_FILES})

