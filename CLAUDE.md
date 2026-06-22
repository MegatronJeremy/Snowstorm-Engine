# CLAUDE.md — Snowstorm Engine

3D game engine with an abstraction over the rendering backend. Currently Vulkan-only (DirectX 12
planned). Windows-only for now. Public domain (`UNLICENSE.txt`). The engine is Hazel-inspired
(`Ref`/`Scope`, `Layer`/`LayerStack`, `SS_*` macros, instrumentation profiler) but has grown its
own EnTT-based ECS, a Systems/Singletons/Services architecture, a Vulkan RHI, an asset system, and
an ImGui editor.

> This repo is consumed as a git **submodule** of `MegatronJeremy/RG2`, but it has its own
> independent history and `master` branch. Develop it as a standalone project.

## Build & run

Toolchain: **CMake + vcpkg**, generating a Visual Studio 2022 solution (toolset `v143`).
Everything is driven by `Scripts/Generate-Solution.py` (bootstraps vcpkg under `vcpkg/`, installs
all packages, configures CMake into `build/`):

```
# from repo root (or double-click Scripts/Generate-Solution.bat, which cd's up a level first):
py Scripts/Generate-Solution.py                 # default triplet x64-windows -> build/Snowstorm.sln
py Scripts/Generate-Solution.py --clean         # wipe build/ first
py Scripts/Generate-Solution.py --fresh         # also wipe vcpkg installed/buildtrees (full reinstall)
```

Then open `build/Snowstorm.sln` and build. **Snowstorm-Editor** is the default startup project;
the debugger working directory is the repo root, so relative `Assets/...` paths resolve. Vulkan
validation layers are wired via the `VK_ADD_LAYER_PATH` env var (set by the script and in the VS
debugger environment) pointing at the vcpkg `bin` dir.

The first run is slow — vcpkg compiles every dependency from source.

## Layout

```
Snowstorm-Core/      # STATIC library: all engine code (the only place most work happens)
  Source/Snowstorm/  #   platform-independent engine (Core, ECS, Render, Systems, ...)
  Source/Platform/   #   Vulkan/ (RHI implementation, ~28 files) and Windows/
Snowstorm-App/       # Sandbox EXECUTABLE — links Core (SandboxApp, Sandbox2D, ParticleSystem)
Snowstorm-Editor/    # Editor EXECUTABLE — links Core; ImGui dockspace, panels, viewport
Assets/              # Shaders, Meshes, Materials, Scenes, Textures (loaded at runtime)
Scripts/             # Generate-Solution.{py,bat}
Tools/dxc/           # DirectX Shader Compiler (HLSL -> SPIR-V)
```

Core builds to a static lib holding code shared by multiple apps; App and Editor are executables
that link Core and add it to their include path. All three targets are **C++20** (the root
`CMakeLists.txt` sets C++17 globally, but every target overrides to 20 — treat the project as C++20).

## Architecture (Core)

- **Entry point:** clients define `Snowstorm::CreateApplication()`; `Core/EntryPoint.hpp` provides
  `main` (inits logging, wraps `Run()` in profiler sessions). `Application` owns the window, the
  `LayerStack`, the `EventBus`, and the `ServiceManager` (singleton via `Application::Get()`).
- **ECS:** EnTT-backed. `World`/`Entity` (`World/`), components in `Components/`, behavior in
  `Systems/` (managed by `SystemManager`), cross-cutting state in `Singletons/` (`SingletonManager`),
  and `Service/` for longer-lived services. Components are registered for reflection via **RTTR**
  (`Components/ComponentRegistration.hpp`).
- **Rendering:** backend-agnostic interfaces in `Render/` (`RendererAPI`, `Renderer`, `Pipeline`,
  `Shader`, `Buffer`, `Texture`, `Material`, `RenderGraph`, ...). The concrete implementation lives
  in `Platform/Vulkan/` (volk + Vulkan Memory Allocator + spirv-reflect; shaders compiled to SPIR-V
  via `Tools/dxc`).
- **Scenes:** serialized to/from JSON (`World/SceneSerializer.hpp`, nlohmann_json).
- **Events:** `Events/` hierarchy dispatched through `EventBus`; input bridged in `Input/`.

### Conventions

- Namespace `Snowstorm`. Smart-pointer aliases `Ref<T>` (shared) / `Scope<T>` (unique) with
  `CreateRef` / `CreateScope` — use these, not raw `std::shared_ptr`/`make_unique`, in engine code.
- Macros from `Core/Base.hpp`: `SS_ASSERT` / `SS_CORE_ASSERT`, `BIT(x)`, `SS_BIND_EVENT_FN(fn)`,
  `SS_DEBUGBREAK()`. Logging is `SS_CORE_*` / `SS_*` (spdlog). Asserts compile out unless `SS_DEBUG`.
- Platform code goes behind `SS_PLATFORM_WINDOWS` (see `Core/PlatformDetection.hpp`); the engine
  currently `#error`s on non-Windows.
- Headers are `.hpp`, translation units `.cpp`. Core globs all sources recursively, so a new file
  under `Snowstorm-Core/Source/` is picked up after re-running CMake (re-generate the solution).

## Dependencies (vcpkg, x64-windows)

assimp, EnTT, fmt, glew, glfw3, glm, imgui (vulkan+glfw bindings, docking), rttr, spdlog, stb,
Vulkan SDK, vulkan-memory-allocator, gli, volk, spirv-reflect, nlohmann-json. The canonical list
is `PACKAGES` in `Scripts/Generate-Solution.py`; the linkage is in `Snowstorm-Core/CMakeLists.txt`.
Keep those two in sync when adding a dependency.

## Git hygiene

`.gitignore` excludes everything generated: `build/`, `vcpkg/`, `.vs/`, `Assets/cache`, and all
solution/project files (`*.sln`, `*.vcxproj*`, `*.cmake`, `CMakeCache.txt`, `ALL_BUILD.*`,
`ZERO_CHECK.*`, `Makefile`). Never commit those or compiled artifacts. Commit messages in English.

## Verify before claiming

- This is graphics code: "renders/looks correct" can only be confirmed by **building and running**
  on a machine with a GPU/display. Headless verification is not possible — say so when you can't run it.
- Confirm behavior against the actual source/build, not from names. Mark unverified statements as
  assumptions.
