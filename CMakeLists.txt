cmake_minimum_required(VERSION 3.16)
project(Snowstorm LANGUAGES CXX)

# The engine's own source root. Every path below is relative to THIS directory rather than to
# CMAKE_SOURCE_DIR, because the engine is not necessarily the top-level project: a game keeps its source
# in its own repository and pulls the engine in with add_subdirectory (the layout Unreal calls a project
# beside the Engine directory, and O3DE calls project-centric engine-source). Under CMAKE_SOURCE_DIR
# those paths would resolve into the GAME's tree and silently find nothing.
set(SNOWSTORM_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# True only when the engine is being configured on its own, not embedded in a game's build.
set(SNOWSTORM_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SNOWSTORM_IS_TOP_LEVEL ON)
endif()

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

# 1. Add subdirectories for core and the executables
# This must happen before setting target properties
add_subdirectory(Snowstorm-Core)
add_subdirectory(Snowstorm-Editor)
add_subdirectory(Snowstorm-Runtime)

# An embedding project owns its own enable_testing(); calling it here would only register CTest against
# the engine's build tree. The test target itself is always defined, so a game can run it if it wants.
if(SNOWSTORM_IS_TOP_LEVEL)
    enable_testing()
endif()
add_subdirectory(Snowstorm-Tests)

# 2. Set the default startup project for Visual Studio/Rider. Only meaningful when the engine is the
# solution being generated; a game sets its own.
if(SNOWSTORM_IS_TOP_LEVEL)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "Snowstorm-Editor")
endif()

# 3. Set working directory for the executable targets
set_property(TARGET Snowstorm-Editor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${SNOWSTORM_ROOT_DIR}")
set_property(TARGET Snowstorm-Runtime PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${SNOWSTORM_ROOT_DIR}")

# Where the Vulkan validation layers live. Exported to the parent scope so an embedding game can put the
# same value on its own targets without re-deriving the engine's vcpkg layout.
set(VCPKG_LAYER_PATH "${SNOWSTORM_ROOT_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/bin")
set(SNOWSTORM_VULKAN_LAYER_PATH "${VCPKG_LAYER_PATH}" CACHE INTERNAL "Vulkan validation layer path shipped with the engine's vcpkg tree")

# Set Vulkan validation layer path environment variable
set(VK_ENV "VK_ADD_LAYER_PATH=${VCPKG_LAYER_PATH}")
set(SNOWSTORM_VS_DEBUGGER_ENVIRONMENT "${VK_ENV}" CACHE INTERNAL "VS debugger environment that makes validation layers discoverable")

set_target_properties(Snowstorm-Editor PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VK_ENV}")
set_target_properties(Snowstorm-Runtime PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VK_ENV}")

# The VS_DEBUGGER_ENVIRONMENT above is Visual-Studio-only: Rider / a bare exe / the terminal don't get it,
# so validation silently fails to load there ("layers requested but not available"). Also bake the absolute
# layer path in as a Debug-only compile definition so EntryPoint can set VK_ADD_LAYER_PATH itself before
# Vulkan init — making validation work regardless of how the app is launched. Debug only (validation is a
# dev tool). Backslashes in the Windows path would break the C string literal, so normalize to forward
# slashes (the Vulkan loader accepts them).
# BUILD_INTERFACE only: this is an absolute path into THIS machine's vcpkg tree, so baking it into the
# installed package would hand a consumer a path that need not exist. An SDK consumer sets
# VK_ADD_LAYER_PATH instead, which is what the game's solution generator does.
string(REPLACE "\\" "/" VCPKG_LAYER_PATH_FWD "${VCPKG_LAYER_PATH}")
target_compile_definitions(Snowstorm-Core PUBLIC
    $<BUILD_INTERFACE:$<$<CONFIG:Debug>:SS_VULKAN_LAYER_PATH="${VCPKG_LAYER_PATH_FWD}">>)

# 4. Browsable Shaders target: surfaces Engine/Shaders/*.hlsl(i) in the IDE so they can be viewed and
# edited alongside the C++, instead of only reachable by opening the files externally. IDE-only
# container (add_custom_target ... SOURCES) -- nothing is compiled or linked here; shaders are still
# compiled at runtime by DXC (hash-cached). Attached to no binary because they're shared engine assets.
file(GLOB SHADER_FILES
    "${SNOWSTORM_ROOT_DIR}/Engine/Shaders/*.hlsl"
    "${SNOWSTORM_ROOT_DIR}/Engine/Shaders/Include/*.hlsli"
)
add_custom_target(Shaders SOURCES ${SHADER_FILES})
# Mirror the on-disk layout in the IDE tree (Shaders/ + Shaders/Include/).
source_group(TREE "${SNOWSTORM_ROOT_DIR}/Engine/Shaders" PREFIX "Shaders" FILES ${SHADER_FILES})

# 5. Install / export, so a game can build against the engine as an SDK instead of embedding its source.
# This is the packaged half of the same contract add_subdirectory gives: either way the consumer links
# Snowstorm::Core and stages the engine's runtime files beside its executable.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(SNOWSTORM_ASSET_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/Snowstorm")
set(SNOWSTORM_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/Snowstorm/cmake")

# Exported as Snowstorm::Core rather than Snowstorm::Snowstorm-Core, matching the in-tree alias below so
# a game spells the dependency the same way whether it embeds the engine or links the installed SDK.
set_target_properties(Snowstorm-Core PROPERTIES EXPORT_NAME Core)

install(TARGETS Snowstorm-Core
    EXPORT SnowstormTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Headers keep their source-tree layout, because that layout IS the include path: Core is included as
# <Snowstorm/...> and <Platform/...>, and Snowstorm.h sits at the root of it.
install(DIRECTORY "${SNOWSTORM_ROOT_DIR}/Snowstorm-Core/Source/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl"
)

# The engine's runtime files. Shaders are compiled at load time by dxc, so the compiler ships too.
install(DIRECTORY "${SNOWSTORM_ROOT_DIR}/Engine/Shaders" "${SNOWSTORM_ROOT_DIR}/Engine/Fonts"
    DESTINATION "${SNOWSTORM_ASSET_INSTALL_DIR}/Engine")
install(DIRECTORY "${SNOWSTORM_ROOT_DIR}/Tools/dxc"
    DESTINATION "${SNOWSTORM_ASSET_INSTALL_DIR}/Tools"
    USE_SOURCE_PERMISSIONS)

install(EXPORT SnowstormTargets
    FILE SnowstormTargets.cmake
    NAMESPACE Snowstorm::
    DESTINATION "${SNOWSTORM_CMAKE_INSTALL_DIR}"
)

configure_package_config_file(
    "${SNOWSTORM_ROOT_DIR}/Scripts/cmake/SnowstormConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SnowstormConfig.cmake"
    INSTALL_DESTINATION "${SNOWSTORM_CMAKE_INSTALL_DIR}"
    PATH_VARS SNOWSTORM_ASSET_INSTALL_DIR
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SnowstormConfigVersion.cmake"
    VERSION 0.1.0
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SnowstormConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SnowstormConfigVersion.cmake"
    DESTINATION "${SNOWSTORM_CMAKE_INSTALL_DIR}"
)

# An alias so the in-tree (add_subdirectory) and installed paths spell the target the same way, and a
# game's CMakeLists does not branch on which one it got.
if(NOT TARGET Snowstorm::Core)
    add_library(Snowstorm::Core ALIAS Snowstorm-Core)
endif()
