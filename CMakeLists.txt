cmake_minimum_required(VERSION 3.16)
project(Snowstorm LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# 1. Add subdirectories for core and the executables
# This must happen before setting target properties
add_subdirectory(Snowstorm-Core)
add_subdirectory(Snowstorm-Editor)
add_subdirectory(Snowstorm-Runtime)

# 2. Set the default startup project for Visual Studio/Rider
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "Snowstorm-Editor")

# 3. Set working directory for the executable targets
set_property(TARGET Snowstorm-Editor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(TARGET Snowstorm-Runtime PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

set(VCPKG_LAYER_PATH "${CMAKE_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/bin")

# Set Vulkan validation layer path environment variable
set(VK_ENV "VK_ADD_LAYER_PATH=${VCPKG_LAYER_PATH}")

set_target_properties(Snowstorm-Editor PROPERTIES VS_DEBUGGER_ENVIRONMENT "{VK_ENV}")
set_target_properties(Snowstorm-Runtime PROPERTIES VS_DEBUGGER_ENVIRONMENT "{VK_ENV}")

