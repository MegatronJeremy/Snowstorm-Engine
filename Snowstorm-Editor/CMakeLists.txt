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

# ImGuizmo: editor-only transform gizmos drawn over the viewport.
find_package(imguizmo CONFIG REQUIRED)

# Link libraries.
# Snowstorm-Core is a STATIC lib whose components self-register via static initializers
# (RTTR_REGISTRATION + AUTO_REGISTER_COMPONENT). The linker drops object files nothing references,
# which would silently skip those initializers — so link Core WHOLE_ARCHIVE to keep every TU.
target_link_libraries(Snowstorm-Editor PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-Core>  # keep self-registration TUs (see note above)
    imguizmo::imguizmo
)

# Copy every runtime DLL this exe depends on next to it after build. vcpkg's automatic applocal deploy
# covers most (assimp, etc.) but missed TracyClient.dll, so launching outside the vcpkg bin dir (e.g. the
# smoke harness) failed with STATUS_DLL_NOT_FOUND. TARGET_RUNTIME_DLLS is the CMake-blessed, complete
# list — copying it explicitly makes the exe self-contained in its output dir. No-op when the list is empty.
add_custom_command(TARGET Snowstorm-Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Snowstorm-Editor> $<TARGET_FILE_DIR:Snowstorm-Editor>
    COMMAND_EXPAND_LISTS
)