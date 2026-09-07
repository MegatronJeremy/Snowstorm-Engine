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

# ---------------------------------------------------------------------------------------------------
# Staging: a directory a game runs out of with no engine tree present.
#
# Deliberately NOT part of the default build. Packaging is a step you ask for, the way Unreal stages on
# package rather than on compile: the payload is ~69 MB, and copying it on every incremental build would
# tax the common case to serve the rare one. It also leaves every existing path alone, so the smoke,
# perf and quality harnesses keep finding executables where they always have.
#
#     cmake --build build --config Debug --target stage
#     cd build/stage/Debug && ./Snowstorm-Pong.exe
#
# What makes the result self-contained is GetEngineRoot's rule 2: it walks up from the executable
# looking for Engine/Shaders, finds it in the stage dir, and never consults the source tree. That is why
# the layout mirrors the repo rather than flattening it, and it is what retires rule 3 (the baked
# configure-time path) for a shipped build.
#
# Engine/cache is deliberately NOT staged: it is generated, and the stage dir must stay writable so a
# cold run can populate it in place.
set(SS_STAGE_DIR "${CMAKE_BINARY_DIR}/stage/$<CONFIG>")
set(SS_STAGE_STAMP "${CMAKE_BINARY_DIR}/stage/stage-$<CONFIG>.stamp")

# CONFIGURE_DEPENDS so adding a shader or a scene re-runs the glob instead of silently staging a stale
# set. The stamp gives the copy a real output for CMake to track: a POST_BUILD copy would only fire when
# an executable RELINKS, so editing a scene and rebuilding would stage the old one.
file(GLOB_RECURSE SS_STAGE_INPUTS CONFIGURE_DEPENDS
    "${SS_ENGINE_SOURCE_DIR}/Engine/Shaders/*"
    "${SS_ENGINE_SOURCE_DIR}/Engine/Fonts/*"
    "${SS_ENGINE_SOURCE_DIR}/Projects/Sandbox/*"
)

add_custom_command(
    OUTPUT "${SS_STAGE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${SS_ENGINE_SOURCE_DIR}/Engine/Shaders" "${SS_STAGE_DIR}/Engine/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${SS_ENGINE_SOURCE_DIR}/Engine/Fonts" "${SS_STAGE_DIR}/Engine/Fonts"
    # dxc is needed only to COMPILE a shader, so a stage shipping a warm cache does not need it. Staged
    # anyway because the shader cache is keyed on source content, and a stage built here may still meet
    # a cold shader elsewhere.
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${SS_ENGINE_SOURCE_DIR}/Tools/dxc" "${SS_STAGE_DIR}/Tools/dxc"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${SS_ENGINE_SOURCE_DIR}/Projects/Sandbox" "${SS_STAGE_DIR}/Projects/Sandbox"
    COMMAND ${CMAKE_COMMAND} -E touch "${SS_STAGE_STAMP}"
    DEPENDS ${SS_STAGE_INPUTS}
    COMMENT "Staging engine and project content"
    VERBATIM
)

add_custom_target(stage DEPENDS "${SS_STAGE_STAMP}")
add_dependencies(stage Snowstorm-Editor Snowstorm-Runtime Snowstorm-Pong)

# The built artifacts. Each executable's whole output DIRECTORY is copied, rather than the exe plus
# $<TARGET_RUNTIME_DLLS>, because that generator expression only names DLLs of directly linked imported
# targets: it misses assimp's own transitive dependencies (minizip, pugixml, poly2tri, zlib), which
# vcpkg's applocal deployment is what actually puts next to the exe. Staging 7 of the 18 DLLs produced a
# stage whose executables would not start at all.
foreach(SS_STAGED_EXE Snowstorm-Editor Snowstorm-Runtime Snowstorm-Pong)
    add_custom_command(TARGET stage POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "$<TARGET_FILE_DIR:${SS_STAGED_EXE}>" "${SS_STAGE_DIR}"
        VERBATIM
    )
endforeach()

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

