# Snowstorm Engine

[![build](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml/badge.svg)](https://github.com/MegatronJeremy/Snowstorm-Engine/actions/workflows/build.yml)

<img width="2558" height="1368" alt="image" src="https://github.com/user-attachments/assets/87f8e9a1-2145-47b6-93fc-c0a009738ed0" />

A 3D game engine with a backend-agnostic renderer, an EnTT-based ECS, and a Dear ImGui editor. The
render abstraction targets **Vulkan** (DirectX 12 planned). Its focus is a **hybrid ray-traced
lighting pipeline** and a **neural super-resolution upscaler**, both benchmarked against
raster/analytic baselines by a built-in metrics harness.

> **Work in progress.** Windows-only for now.

## Features

- **Rendering.** Backend-agnostic interfaces (`Renderer`, `Pipeline`, `Shader`, `Material`,
  `RenderGraph`) with a Vulkan backend on volk, Vulkan Memory Allocator, and SPIR-V reflection.
  Bindless textures, a render graph with automatic resource barriers, a dedicated transfer queue,
  GPU-timestamped passes, and an HDR (RGBA16F) target with ACES tonemapping.
- **Hybrid ray tracing (hardware ray query).** TLAS/BLAS driving ray-traced shadows, ambient
  occlusion, reflections, and global illumination, each with temporal accumulation and an SVGF
  edge-avoiding à-trous denoiser. Falls back to raster/analytic baselines on non-RT GPUs.
- **Neural super-resolution.** Spatial and temporal CNN refiners running as Vulkan compute passes
  (fp16 or fp32) over an internal-resolution render. PyTorch harness (`Tools/neural/`) exports
  byte-parity `.ssnn` weights.
- **Anti-aliasing and upscaling.** TAA (camera jitter, velocity pass, temporal resolve) with a
  post-tonemap contrast-adaptive sharpen; FXAA as an alternative.
- **Evaluation harness.** Split-screen A/B (upscaled vs full-res), a GPU PSNR/SSIM pass, a
  deterministic benchmark camera path, and a training-dataset exporter.
- **PBR and lighting.** Metallic-roughness materials with normal/AO/emissive maps, a procedural
  sky, compute-baked IBL (irradiance, prefilter, BRDF LUT), and directional/point/spot shadow maps
  with hardware PCF.
- **ECS.** EnTT-based, split into phased Systems, Singletons, and Services, with RTTR component
  reflection, native C++ scripting, and an opt-in data-parallel path (`ParallelForEach` /
  `ParallelGather`) over the job system.
- **Editor.** ImGui dockspace with scene hierarchy, inspector, viewport (ImGuizmo gizmos,
  click-to-select, camera framing), content browser, undo/redo, a performance panel (per-system CPU
  and per-pass GPU timings), a live CVar panel, and a developer console with autocomplete.
- **Projects, assets, scenes.** `.ssproj` projects; mesh/material/texture assets (assimp, stb)
  cooked to binary caches and loaded asynchronously off the main thread; JSON scene serialization;
  HLSL shaders compiled to SPIR-V (`dxc`) async, cached, and hot-reloaded.
- **Console variables.** Typed CVar registry resolved from defaults, config file
  (`SnowstormConfig.cfg`), env, and CLI, live-editable in the editor; gates shadows, RT effects, the
  upscaler, IBL, exposure, and validation.
- **Foundations.** Layer stack, event bus, input, a job-system thread pool, spdlog logging, and
  Tracy profiling (live) with a headless Chrome-tracing JSON fallback.
- **Tested and CI'd.** Catch2 unit tests, a headless smoke-test harness, a golden-file GPU
  perf-benchmark gate, and GitHub Actions for build, clang-format lint, and shader compilation.

## Tech stack

C++20 · CMake · vcpkg · Vulkan (ray query) · GLFW · GLM · EnTT · Dear ImGui (+ ImGuizmo) · spdlog ·
assimp · RTTR · Vulkan Memory Allocator · volk · SPIRV-Reflect · nlohmann/json · stb · Tracy ·
Catch2 · PyTorch (neural training)

## Getting started

### Prerequisites

- Windows, Visual Studio 2022 (toolset `v143`)
- CMake 3.16+, Python 3, Git

vcpkg and all dependencies are bootstrapped by the generation script. The first run is slow because
vcpkg builds every dependency from source.

### Build & run

```bat
:: from the repository root; --clean wipes build/ first, --fresh also reinstalls vcpkg packages
py Scripts\Generate-Solution.py
```

The script bootstraps vcpkg into `vcpkg/`, installs dependencies, and configures CMake into
`build/`. Open `build/Snowstorm.sln` and build. **Snowstorm-Editor** is the default startup project;
the debugger working directory is the repo root so relative `Engine/...` and `Projects/...` paths
resolve. Vulkan validation layers are wired via `VK_ADD_LAYER_PATH`.

## Project structure

| Project | Output | Description |
| --- | --- | --- |
| **Snowstorm-Core** | static library | All engine code: platform-independent under `Source/Snowstorm/`, backend under `Source/Platform/` (Vulkan, Windows). |
| **Snowstorm-Editor** | executable | The editor (ImGui dockspace, hierarchy, viewport); default startup project. |
| **Snowstorm-Runtime** | executable | Editor-free player: runs the same systems without tooling and blits the primary camera to the swapchain. |
| **Snowstorm-Tests** | executable | Catch2 unit tests (run via CTest). |

```
Engine/            engine-owned runtime assets: Shaders/ (HLSL), Fonts/, cooked caches
Projects/Sandbox/  the default project (.ssproj) with its own assets/ (Meshes, Materials, Scenes)
Scripts/           Generate-Solution.py/.bat, smoke-test.py, perf-bench.py
Tools/dxc/         DirectX Shader Compiler (HLSL -> SPIR-V)
Tools/neural/      PyTorch training harness for the neural upscaler (exports .ssnn weights)
Tools/tracy/       Tracy profiler GUI (connect to a running Debug build)
```

Executables link the Core static library and add its `Source/` directory to their include path.

## Testing

```bat
build\Snowstorm-Tests\Debug\Snowstorm-Tests.exe   :: Catch2 unit tests
py Scripts\smoke-test.py                           :: boots each exe for N frames, checks crashes/errors
py Scripts\perf-bench.py                           :: averages per-pass GPU timings, diffs vs baseline
```

The smoke test and perf benchmark need a real GPU/display (Vulkan), so they are local gates, not CI
jobs (hosted CI only compiles).

## Documentation

Architecture, conventions, and the full build/debug workflow are in [`AGENTS.md`](AGENTS.md). The
roadmap lives in [GitHub issues](https://github.com/MegatronJeremy/Snowstorm-Engine/issues).

## License

Public domain, see [`UNLICENSE.txt`](UNLICENSE.txt).
