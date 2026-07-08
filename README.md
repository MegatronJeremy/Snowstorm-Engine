# Snowstorm Engine

[![build](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml/badge.svg)](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml)

<img width="2557" height="1388" alt="Snowstorm Editor" src="https://github.com/user-attachments/assets/8ff3be0c-e31f-40ec-9767-c9af7460a772" />

A 3D game engine with a backend-agnostic renderer, an EnTT-based entity-component-system, and a
Dear ImGui editor. The rendering abstraction currently targets **Vulkan**; DirectX 12 is planned.

> **Work in progress.** Windows-only for now.

## Features

- **Backend-agnostic rendering** — engine-facing interfaces (`Renderer`, `Pipeline`, `Shader`,
  `Material`, `RenderGraph`, ...) with a Vulkan implementation built on volk, Vulkan Memory
  Allocator, and SPIR-V reflection. Bindless textures, a render-graph with automatic resource
  transitions, a dedicated transfer queue for uploads, and GPU-timestamped passes.
- **PBR & lighting** — metallic-roughness materials with normal/AO/emissive maps, a procedural sky,
  image-based lighting (compute-baked irradiance/prefilter/BRDF), directional + spot shadow maps,
  and alpha-cutout support.
- **Entity-Component-System** — built on [EnTT](https://github.com/skypjack/entt), organised into
  Systems (phased), Singletons, and Services, with RTTR-based component reflection.
- **Editor** — Dear ImGui dockspace with scene hierarchy, inspector, viewport (ImGuizmo transform
  gizmos, click-to-select, camera framing), content browser, a performance panel (per-system CPU +
  per-pass GPU timings), and a developer console with live log stream + command input +
  autocomplete.
- **Asset & scene pipeline** — mesh/material/texture assets (assimp, stb, gli) compiled to cooked
  binary caches, loaded **asynchronously** off the main thread via a job system (with a loading
  bar); JSON scene serialization. Shaders (HLSL → SPIR-V via `dxc`) also compile async + cached.
- **Console variables** — a typed CVar registry (env / CLI / live-editable from the editor) gating
  engine flags like shadows, IBL, exposure, and validation.
- **Engine foundations** — layer stack, event bus, input handling, a job-system thread pool,
  logging (spdlog), and profiling via **Tracy** (live) with a headless Chrome-tracing JSON fallback.
- **Tested & CI'd** — Catch2 unit tests, a headless smoke-test harness, and GitHub Actions for
  build / clang-format lint / shader compilation.

## Tech stack

C++20 · CMake · vcpkg · Vulkan · GLFW · GLM · EnTT · Dear ImGui (+ ImGuizmo) · spdlog · assimp ·
RTTR · Vulkan Memory Allocator · volk · SPIRV-Reflect · nlohmann/json · stb · gli · Tracy · Catch2

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
| **Snowstorm-Tests** | executable | Catch2 unit tests (run via CTest). |

```
Assets/       runtime assets (Shaders, Meshes, Materials, Scenes, Textures) + cooked caches
Scripts/      solution generation (Generate-Solution.py / .bat) + smoke-test.py
Tools/dxc/    DirectX Shader Compiler (HLSL -> SPIR-V)
Tools/tracy/  Tracy profiler GUI (connect to a running Debug build)
```

Executables link the Core static library and add its `Source/` directory to their include path.

## Testing

```bat
:: unit tests (after building)
build\Snowstorm-Tests\Debug\Snowstorm-Tests.exe

:: headless smoke test — boots each executable for N frames and checks for crashes / errors
py Scripts\smoke-test.py
```

The smoke test needs a real GPU/display (Vulkan), so it is a local gate, not a CI job.

## License

Public domain — see [`UNLICENSE.txt`](UNLICENSE.txt).
