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
)