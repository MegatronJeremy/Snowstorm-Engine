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
find_package(Tracy CONFIG REQUIRED)

# Tracy is only compiled in when TRACY_ENABLE is defined; otherwise every Tracy macro is a no-op (zero
# overhead). Enable it in Debug only, matching the engine's SS_PROFILE gating — profiling a Release build
# is a deliberate opt-in you'd do by adding this define to that config, not the default.
target_compile_definitions(Snowstorm-Core PUBLIC $<$<CONFIG:Debug>:TRACY_ENABLE>)

target_include_directories(Snowstorm-Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${Stb_INCLUDE_DIR}
)

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