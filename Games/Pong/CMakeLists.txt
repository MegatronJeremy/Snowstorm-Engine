# Pong: the example game. It exists to show, in a few hundred readable lines, the whole of what
# building on Snowstorm takes: link Snowstorm-Core, declare a couple of components, register one system through the same
# RegisterPongSystems seam any game uses, and author a scene.
#
# It has no third-party dependency and no network fetch, so it is always built and can be run by
# anything, including the headless smoke harness.
cmake_minimum_required(VERSION 3.15)
project(PongGame VERSION 1.0 LANGUAGES CXX)

file(GLOB_RECURSE PONG_GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_library(PongGame STATIC ${PONG_GAME_SOURCES})

set_target_properties(PongGame PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    FOLDER Games
)

target_include_directories(PongGame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(PongGame PUBLIC Snowstorm-Core)

add_executable(Snowstorm-Pong "${CMAKE_CURRENT_SOURCE_DIR}/PongMain.cpp")

set_target_properties(Snowstorm-Pong PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    FOLDER Games
)

# WHOLE_ARCHIVE for the reason every host here needs it: the component registrations in
# PongComponents.cpp are static initializers in a TU nothing references, and dropping them is silent.
target_link_libraries(Snowstorm-Pong PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,Snowstorm-Core>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,PongGame>
)

add_custom_command(TARGET Snowstorm-Pong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Snowstorm-Pong> $<TARGET_FILE_DIR:Snowstorm-Pong>
    COMMAND_EXPAND_LISTS
)
