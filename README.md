# Snowstorm Engine

<img width="2557" height="1388" alt="Snowstorm Editor" src="https://github.com/user-attachments/assets/8ff3be0c-e31f-40ec-9767-c9af7460a772" />

A 3D game engine with a backend-agnostic renderer, an EnTT-based entity-component-system, and a
Dear ImGui editor. The rendering abstraction currently targets **Vulkan**; DirectX 12 is planned.

> **Work in progress.** Windows-only for now.

## Features

- **Backend-agnostic rendering** — engine-facing interfaces (`Renderer`, `Pipeline`, `Shader`,
  `Material`, `RenderGraph`, ...) with a Vulkan implementation built on volk, Vulkan Memory
  Allocator, and SPIR-V reflection. HLSL shaders are compiled to SPIR-V via `dxc`.
- **Entity-Component-System** — built on [EnTT](https://github.com/skypjack/entt), organised into
  Systems, Singletons, and Services, with RTTR-based component reflection.
- **Editor** — Dear ImGui dockspace with a scene hierarchy panel, viewport, and notifications.
- **Asset & scene pipeline** — mesh/material/texture assets (assimp, stb, gli) and JSON scene
  serialization.
- **Engine foundations** — layer stack, event bus, input handling, logging (spdlog), and a
  built-in profiler that emits Chrome-tracing JSON.

## Tech stack

C++20 · CMake · vcpkg · Vulkan · GLFW · GLM · EnTT · Dear ImGui · spdlog · assimp · RTTR ·
Vulkan Memory Allocator · volk · SPIRV-Reflect · nlohmann/json · stb · gli

## Getting started

### Prerequisites

- Windows + Visual Studio 2022 (toolset `v143`)
- CMake 3.16+
- Python 3 (for the generation script)
- Git

vcpkg and all third-party dependencies are bootstrapped and installed automatically by the
generation script — you do not need to install them by hand. The first run is slow because vcpkg
compiles every dependency from source.

### Build & run

```bat
:: from the repository root
py Scripts\Generate-Solution.py
```

or double-click `Scripts\Generate-Solution.bat` (it changes into the repo root first). Useful flags:

```bat
py Scripts\Generate-Solution.py --clean    :: delete build/ before configuring
py Scripts\Generate-Solution.py --fresh    :: also reinstall all vcpkg packages from scratch
```

The script bootstraps vcpkg into `vcpkg/`, installs the dependencies, and configures CMake into
`build/`. Open the generated **`build/Snowstorm.sln`** in Visual Studio and build.
**Snowstorm-Editor** is the default startup project, and the debugger working directory is set to
the repository root so that relative `Assets/...` paths resolve. Vulkan validation layers are wired
up automatically via the `VK_ADD_LAYER_PATH` environment variable.

## Project structure

| Project | Output | Description |
| --- | --- | --- |
| **Snowstorm-Core** | static library | All engine code: platform-independent code under `Source/Snowstorm/`, backend code under `Source/Platform/` (Vulkan, Windows). |
| **Snowstorm-Editor** | executable | The editor (ImGui dockspace, scene hierarchy, viewport); the default startup project. |
| **Snowstorm-Runtime** | executable | Editor-free "player": runs the same engine systems as the editor without any tooling. Work in progress — see `docs/RUNTIME_REFACTOR.md`. |

```
Assets/      runtime assets (Shaders, Meshes, Materials, Scenes, Textures)
Scripts/     solution generation (Generate-Solution.py / .bat)
Tools/dxc/   DirectX Shader Compiler (HLSL -> SPIR-V)
```

Executables link the Core static library and add its `Source/` directory to their include path.

## License

Public domain — see [`UNLICENSE.txt`](UNLICENSE.txt).
