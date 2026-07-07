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

# Link Core for the code under test (e.g. UUID); Catch2WithMain provides main(). WHOLE_ARCHIVE so the
# component self-registration static initializers run (SceneSerializer round-trip needs the reflection).
target_link_libraries(Snowstorm-Tests PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-Core>
    Catch2::Catch2WithMain
)

# Copy dependent runtime DLLs next to the test exe (Core drags in TracyClient.dll; see
# Snowstorm-Editor/CMakeLists.txt for the rationale).
add_custom_command(TARGET Snowstorm-Tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Snowstorm-Tests> $<TARGET_FILE_DIR:Snowstorm-Tests>
    COMMAND_EXPAND_LISTS
)

# Run the whole suite as one CTest entry (Catch2 returns non-zero on any failure).
add_test(NAME unit COMMAND Snowstorm-Tests)
