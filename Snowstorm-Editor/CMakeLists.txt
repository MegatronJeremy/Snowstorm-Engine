# Snowstorm-Editor CMake Configuration
cmake_minimum_required(VERSION 3.15)
project(Snowstorm-Editor VERSION 1.0 LANGUAGES CXX)

# ImGuizmo: editor-only transform gizmos drawn over the viewport.
find_package(imguizmo CONFIG REQUIRED)

# The editor is a static library plus a thin executable, so a project can build its own editor executable
# around the library with IEditorExtensions (Source/Extension/EditorExtension.hpp). The library is everything
# but the entry point.
file(GLOB_RECURSE EDITOR_LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)
list(REMOVE_ITEM EDITOR_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/SnowstormEditor.cpp")

add_library(Snowstorm-EditorLib STATIC)
set_target_properties(Snowstorm-EditorLib PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_sources(Snowstorm-EditorLib PRIVATE ${EDITOR_LIB_SOURCES})
target_include_directories(Snowstorm-EditorLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${SS_ENGINE_SOURCE_DIR}/Snowstorm-Core/Source
)
# Snowstorm-Core is a STATIC lib whose components self-register via static initializers
# (RTTR_REGISTRATION + AUTO_REGISTER_COMPONENT). The linker drops object files nothing references, which
# would silently skip those initializers, so Core is linked WHOLE_ARCHIVE. PUBLIC on the library, so every
# editor executable built on it inherits that without repeating it.
target_link_libraries(Snowstorm-EditorLib PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-Core>
    imguizmo::imguizmo
)

# Games this editor can author and play. Linked exactly as Unreal's editor loads the game module.
# WHOLE_ARCHIVE for the same reason Core needs it: a game's component registrations are static
# initializers in TUs nothing references, and dropping them fails SILENTLY (the component vanishes from
# the inspector and the serializer skips it), never as a link error. On the library, since the EditorLayer
# registers the game's systems.
target_link_libraries(Snowstorm-EditorLib PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,PongGame>
)
add_library(Snowstorm::Editor ALIAS Snowstorm-EditorLib)

add_executable(Snowstorm-Editor)
set_target_properties(Snowstorm-Editor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_sources(Snowstorm-Editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/SnowstormEditor.cpp")
# The editor library self-registers components too (the Mandelbrot example), so it is WHOLE_ARCHIVE as well.
target_link_libraries(Snowstorm-Editor PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-EditorLib>
)

# Copy every runtime DLL this exe depends on next to it after build. vcpkg's automatic applocal deploy
# covers most (assimp, etc.) but missed TracyClient.dll, so launching outside the vcpkg bin dir (e.g. the
# smoke harness) failed with STATUS_DLL_NOT_FOUND. TARGET_RUNTIME_DLLS is the CMake-blessed, complete
# list: copying it explicitly makes the exe self-contained in its output dir. No-op when the list is empty.
add_custom_command(TARGET Snowstorm-Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Snowstorm-Editor> $<TARGET_FILE_DIR:Snowstorm-Editor>
    COMMAND_EXPAND_LISTS
)
