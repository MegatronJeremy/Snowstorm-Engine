# Snowstorm Engine

[![build](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml/badge.svg)](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml)

<img width="100%" alt="Snowstorm Engine editor" src="https://github.com/user-attachments/assets/ea6a7730-0c85-4a3b-bcb0-da7457581f77" />

A 3D game engine with a backend-agnostic renderer, an EnTT-based entity-component-system, and a
Dear ImGui editor. The rendering abstraction currently targets **Vulkan**; DirectX 12 is planned.
Its focus is a **hybrid ray-traced lighting pipeline** and a **neural super-resolution upscaler**,
both benchmarked against raster/analytic baselines by a built-in metrics harness.

> **Work in progress.** Windows-only for now.

## Features

- **Backend-agnostic rendering** — engine-facing interfaces (`Renderer`, `Pipeline`, `Shader`,
  `Material`, `RenderGraph`, ...) with a Vulkan implementation built on volk, Vulkan Memory
  Allocator, and SPIR-V reflection. Bindless textures, a render-graph with automatic resource
  transitions, a dedicated transfer queue for uploads, GPU-timestamped passes, and an HDR
  (RGBA16F) scene-color target with tonemapping.
- **Hybrid ray tracing (hardware ray query)** — TLAS/BLAS acceleration structures driving ray-traced
  shadows, ambient occlusion (RTAO), reflections, and global illumination, each with temporal
  accumulation and SVGF-style edge-avoiding à-trous denoisers. Effects degrade gracefully to raster
  / analytic baselines on a non-RT GPU.
- **Neural super-resolution** — a spatial + temporal (DLSS/XeSS-style) CNN refiner running as
  Vulkan compute passes over an internal-resolution render, with a companion PyTorch training
  harness (`Tools/neural/`) that exports byte-parity `.ssnn` weights.
- **Temporal anti-aliasing & upscaling** — camera jitter + a motion-vector (velocity) pass + a
  temporal resolve, with a post-tonemap contrast-adaptive sharpen; selectable alongside FXAA.
- **Evaluation harness** — split-screen A/B (upscaled vs full-res ground truth), a GPU PSNR/SSIM
  metrics pass, a deterministic benchmark camera path, and a training-dataset exporter — the
  apparatus for measuring the RT and neural work.
- **PBR & lighting** — metallic-roughness materials with normal/AO/emissive maps, a procedural sky,
  image-based lighting (compute-baked irradiance/prefilter/BRDF), shadow maps (raster + hardware
  PCF) with alpha-cutout support.
- **Entity-Component-System** — built on [EnTT](https://github.com/skypjack/entt), organised into
  Systems (phased), Singletons, and Services, with RTTR-based component reflection and an opt-in
  data-parallel path (`ParallelForEach` / `ParallelGather`) over the job system.
- **Editor** — Dear ImGui dockspace with scene hierarchy, inspector, viewport (ImGuizmo transform
  gizmos, click-to-select, camera framing), content browser, a performance panel (per-system CPU +
  per-pass GPU timings), a live CVar console-variables panel, and a developer console with log
  stream + command input + autocomplete.
- **Projects, assets & scenes** — a `.ssproj` project system; mesh/material/texture assets (assimp,
  stb, gli) compiled to cooked binary caches, loaded **asynchronously** off the main thread via a
  job system (with a loading bar); JSON scene serialization. Shaders (HLSL → SPIR-V via `dxc`) also
  compile async + cached.
- **Console variables** — a typed CVar registry (config file / env / CLI / live-editable from the
  editor, persisted to `SnowstormConfig.cfg`) gating engine flags across shadows, RT effects, the
  upscaler, IBL, exposure, and validation.
- **Engine foundations** — layer stack, event bus, input handling, a job-system thread pool,
  logging (spdlog), and profiling via **Tracy** (live) with a headless Chrome-tracing JSON fallback.
- **Tested & CI'd** — Catch2 unit tests, a headless smoke-test harness, a golden-file GPU
  perf-benchmark gate, and GitHub Actions for build / clang-format lint / shader compilation.

## Tech stack

C++20 · CMake · vcpkg · Vulkan (ray query) · GLFW · GLM · EnTT · Dear ImGui (+ ImGuizmo) · spdlog ·
assimp · RTTR · Vulkan Memory Allocator · volk · SPIRV-Reflect · nlohmann/json · stb · gli · Tracy ·
Catch2 · PyTorch (neural training)

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
the repository root so that relative `Engine/...` and `Projects/...` paths resolve. Vulkan
validation layers are wired up automatically via the `VK_ADD_LAYER_PATH` environment variable.

## Project structure

| Project | Output | Description |
| --- | --- | --- |
| **Snowstorm-Core** | static library | All engine code: platform-independent code under `Source/Snowstorm/`, backend code under `Source/Platform/` (Vulkan, Windows). |
| **Snowstorm-Editor** | executable | The editor (ImGui dockspace, scene hierarchy, viewport); the default startup project. |
| **Snowstorm-Runtime** | executable | Editor-free "player": runs the same engine systems as the editor without any tooling. Work in progress — the non-ImGui present path is still open ([#4](https://github.com/MegatronJeremy/Snowstorm-Engine/issues/4)). |
| **Snowstorm-Tests** | executable | Catch2 unit tests (run via CTest). |

```
Engine/            engine-owned runtime assets: Shaders/ (HLSL), Fonts/, and cooked caches
Projects/Sandbox/  the default project (.ssproj) — its own assets/ (Meshes, Materials, Scenes, ...)
Scripts/           solution generation (Generate-Solution.py / .bat), smoke-test.py, perf-bench.py
Tools/dxc/         DirectX Shader Compiler (HLSL -> SPIR-V)
Tools/neural/      PyTorch training harness for the neural upscaler (exports .ssnn weights)
Tools/tracy/       Tracy profiler GUI (connect to a running Debug build)
```

Executables link the Core static library and add its `Source/` directory to their include path.

## Testing

```bat
:: unit tests (after building)
build\Snowstorm-Tests\Debug\Snowstorm-Tests.exe

:: headless smoke test — boots each executable for N frames and checks for crashes / errors
py Scripts\smoke-test.py

:: GPU perf benchmark — averages per-pass GPU timings and diffs against a committed baseline
py Scripts\perf-bench.py
```

The smoke test and perf benchmark need a real GPU/display (Vulkan), so they are local gates, not CI
jobs (hosted CI only compiles).

## Documentation

Architecture, conventions, and the full build/debug workflow live in
[`AGENTS.md`](AGENTS.md). The roadmap is tracked in
[GitHub issues](https://github.com/MegatronJeremy/Snowstorm-Engine/issues).

## License

Public domain — see [`UNLICENSE.txt`](UNLICENSE.txt).
