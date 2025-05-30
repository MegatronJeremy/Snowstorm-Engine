cmake_minimum_required(VERSION 3.16)
project(Snowstorm LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Add subdirectories for core and app
add_subdirectory(Snowstorm-Core)
add_subdirectory(Snowstorm-App)
add_subdirectory(Snowstorm-Editor)

# Set the default startup project for Visual Studio
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "Snowstorm-Editor")

# Set working directory to root directory
set_property(TARGET Snowstorm-Editor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(TARGET Snowstorm-App PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")