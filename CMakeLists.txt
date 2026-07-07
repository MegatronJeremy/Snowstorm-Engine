cmake_minimum_required(VERSION 3.16)
project(Snowstorm LANGUAGES CXX)

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

# 4. Browsable Shaders target: surfaces Assets/Shaders/*.hlsl(i) in the IDE so they can be viewed and
# edited alongside the C++, instead of only reachable by opening the files externally. IDE-only
# container (add_custom_target ... SOURCES) -- nothing is compiled or linked here; shaders are still
# compiled at runtime by DXC (hash-cached). Attached to no binary because they're shared engine assets.
file(GLOB SHADER_FILES
    "${CMAKE_SOURCE_DIR}/Assets/Shaders/*.hlsl"
    "${CMAKE_SOURCE_DIR}/Assets/Shaders/Include/*.hlsli"
)
add_custom_target(Shaders SOURCES ${SHADER_FILES})
# Mirror the on-disk layout in the IDE tree (Shaders/ + Shaders/Include/).
source_group(TREE "${CMAKE_SOURCE_DIR}/Assets/Shaders" PREFIX "Shaders" FILES ${SHADER_FILES})

