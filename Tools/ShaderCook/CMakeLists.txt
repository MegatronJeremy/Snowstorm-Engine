# Standalone build of the offline shader cook: header-only dependency on ShaderCompile.hpp, no vcpkg,
# so the shaders CI job can build it without configuring the engine.
cmake_minimum_required(VERSION 3.15)
project(ShaderCook LANGUAGES CXX)

add_executable(ShaderCook ShaderCook.cpp)
set_target_properties(ShaderCook PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(ShaderCook PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../Snowstorm-Core/Source)
