# Snowstorm-Runtime CMake Configuration
cmake_minimum_required(VERSION 3.15)
project(Snowstorm-Runtime VERSION 1.0 LANGUAGES CXX)

add_executable(Snowstorm-Runtime)

# Set C++ standard
set_target_properties(Snowstorm-Runtime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Add source files
file(GLOB_RECURSE RUNTIME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)
target_sources(Snowstorm-Runtime PRIVATE ${RUNTIME_SOURCES})

# Include directories
target_include_directories(Snowstorm-Runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/Snowstorm-Core/Source
)

# Link libraries.
# Core's components self-register via static initializers; link WHOLE_ARCHIVE so the linker keeps
# those TUs instead of dropping the unreferenced ones (see Snowstorm-Editor/CMakeLists.txt).
target_link_libraries(Snowstorm-Runtime PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-Core>
)
