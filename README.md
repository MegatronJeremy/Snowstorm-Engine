# Snowstorm Engine

![image](https://github.com/user-attachments/assets/6fc4c33b-fe00-4a78-911f-7565fae1bf15)
(Work in progress)

3D game engine with an abstract rendering backend. Currently supports Vulkan, and will support DirectX12 in the future.

There are three included projects - __Core__, __App__ and __Editor__. __CMake__ in conjunction with __vcpkg__ is used to generate project files.

Core builds into a static library and is meant to contain common code intended for use in multiple applications. 
App and Editor build into an executable and link the Core static library, as well as having an include path to Core's code.

The `Scripts/` directory contains a generate solution script for a Visual Studio solution file, as well as a build script.

## License
- UNLICENSE for this repository (see `UNLICENSE.txt` for more details)
