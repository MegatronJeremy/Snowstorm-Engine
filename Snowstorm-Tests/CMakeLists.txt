# Snowstorm-Tests CMake Configuration
cmake_minimum_required(VERSION 3.15)
project(Snowstorm-Tests VERSION 1.0 LANGUAGES CXX)

add_executable(Snowstorm-Tests)

set_target_properties(Snowstorm-Tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

file(GLOB_RECURSE TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)
target_sources(Snowstorm-Tests PRIVATE ${TEST_SOURCES})

target_include_directories(Snowstorm-Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Snowstorm-Core/Source
)

find_package(Catch2 CONFIG REQUIRED)

# Link Core for the code under test (e.g. UUID); Catch2WithMain provides main().
target_link_libraries(Snowstorm-Tests PRIVATE
    Snowstorm-Core
    Catch2::Catch2WithMain
)

# Run the whole suite as one CTest entry (Catch2 returns non-zero on any failure).
add_test(NAME unit COMMAND Snowstorm-Tests)
