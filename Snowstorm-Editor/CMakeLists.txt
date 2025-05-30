# Snowstorm-Editor CMake Configuration
cmake_minimum_required(VERSION 3.15)
project(Snowstorm-Editor VERSION 1.0 LANGUAGES CXX)

add_executable(Snowstorm-Editor)

# Set C++ standard
set_target_properties(Snowstorm-Editor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Add source files
file(GLOB_RECURSE EDITOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)
target_sources(Snowstorm-Editor PRIVATE ${EDITOR_SOURCES})

# Include directories
target_include_directories(Snowstorm-Editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/Snowstorm-Core/Source
)

# Link libraries
target_link_libraries(Snowstorm-Editor PUBLIC
    Snowstorm-Core  # Link the core library
)