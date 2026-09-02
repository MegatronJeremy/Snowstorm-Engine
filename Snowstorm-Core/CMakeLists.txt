cmake_minimum_required(VERSION 3.15)

add_library(Snowstorm-Core STATIC)

set_target_properties(Snowstorm-Core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

file(GLOB_RECURSE CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)
target_sources(Snowstorm-Core PRIVATE ${CORE_SOURCES})

# Precompiled header. pch.h existed and was #included by ~24 TUs but was NEVER actually precompiled
# (no target_precompile_headers), so every TU re-parsed <Windows.h>/spdlog/STL from scratch. Wiring it
# here force-includes it into all Core TUs and compiles it once. PRIVATE: the PCH is a Core build-speed
# detail, not part of Core's public interface — executables get their own (or none).
target_precompile_headers(Snowstorm-Core PRIVATE Source/pch.h)

find_package(assimp CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(rttr CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(gli CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Header-only, so there is no package config and no library to link: just an include path.
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)
find_package(Tracy CONFIG REQUIRED)

# Tracy is only compiled in when TRACY_ENABLE is defined; otherwise every Tracy macro is a no-op (zero
# overhead). Enable it in Debug only, matching the engine's SS_PROFILE gating — profiling a Release build
# is a deliberate opt-in you'd do by adding this define to that config, not the default.
target_compile_definitions(Snowstorm-Core PUBLIC $<$<CONFIG:Debug>:TRACY_ENABLE>)

target_include_directories(Snowstorm-Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${Stb_INCLUDE_DIR}
)

# PRIVATE: miniaudio is an implementation detail of AudioService, whose header deliberately hides it
# (~90k lines) behind a pimpl, so nothing downstream should be able to include it by accident.
target_include_directories(Snowstorm-Core PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

# The single TU that compiles miniaudio. Skipping the PCH matters: pch.h is force-included into every
# Core source, and miniaudio expects to be the first thing in its own translation unit. /W0 because its
# warnings are upstream's and would bury the engine's own.
set_source_files_properties(Source/Snowstorm/Audio/MiniaudioImpl.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
)

# Doom's sound backend includes doomtype.h, which in C++ typedefs `boolean` as unsigned int while
# Windows' rpcndr.h typedefs it as unsigned char. pch.h force-includes Windows.h everywhere else, so
# this one TU has to be built without it.
set_source_files_properties(Source/Snowstorm/Examples/Doom/DoomSound.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
)
if(MSVC)
    set_source_files_properties(Source/Snowstorm/Audio/MiniaudioImpl.cpp PROPERTIES
        COMPILE_OPTIONS "/W0"
    )
endif()

target_link_libraries(Snowstorm-Core PUBLIC
    assimp::assimp
    EnTT::EnTT
    fmt::fmt
    GLEW::GLEW
    glfw
    glm::glm
    imgui::imgui
    RTTR::Core
    spdlog::spdlog
    Vulkan::Vulkan
    Vulkan::Headers 
    volk::volk 
    volk::volk_headers
    GPUOpen::VulkanMemoryAllocator
    gli
    unofficial::spirv-reflect
    nlohmann_json::nlohmann_json
    Tracy::TracyClient
)

# Embedded Doom (root option SS_ENABLE_DOOM, default OFF). Source/Snowstorm/Examples/Doom compiles
# either way; without this target its SS_HAS_DOOM define is absent and DoomSystem is a no-op, so the
# GPL-2.0 dependency stays out of a default build entirely. On Core rather than an executable so both
# the Editor and the Runtime get it from RegisterCoreSystems.
if(SS_ENABLE_DOOM)
    target_link_libraries(Snowstorm-Core PUBLIC doomgeneric)
endif()
