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

## Smoke test (run after non-trivial changes)

`Scripts/smoke-test.py` boots each executable headlessly and checks it doesn't crash or log
errors. It launches every app with `SS_SMOKE_FRAMES` set (the engine then runs that many frames
and exits cleanly), captures stdout/stderr, enforces a per-app wall-clock timeout (a hang/deadlock
becomes a failure instead of blocking), checks the exit code, and scans the log for error markers
(`[error]`/`[critical]`, Vulkan validation, assertion text). Exit 0 = all pass.

```
py Scripts/smoke-test.py                 # 120 frames, 60s timeout/app, Debug build
py Scripts/smoke-test.py --frames 300    # longer soak
py Scripts/smoke-test.py --only Editor   # single target (Editor | Runtime)
py Scripts/smoke-test.py --warnings-fail # treat [warning] lines as failures too
py Scripts/smoke-test.py --strict        # enable deeper Vulkan validation (see below)
```

`--strict` sets `SS_VALIDATION_EXTRA=1`, which enables **synchronization validation** (barrier/
semaphore/fence hazards) and **best-practices** (perf/usage foot-guns) via `VkValidationFeaturesEXT`.
These are off by default — they add overhead and best-practices is advisory/noisy. Strict findings
are logged at `[warning]` level and shown as **notes**, not failures (a strict run still PASSes on
them); add `--warnings-fail` to gate on them. Genuine validation **errors** are always `[error]`
level and fail the run in either mode. GPU-assisted validation is not wired up (much heavier) — add
it when the compute path needs it.

**Run it after any change substantial enough to affect runtime behavior** (engine/render/ECS/asset
code, the frame loop, anything touching Vulkan) — not for docs/comment/build-script-only edits.
Build first (`cmake --build build --config Debug`), then smoke-test. It needs a **real GPU/display**
(Vulkan), so it is a **local** gate — it cannot run on hosted CI; the GitHub `build` workflow only
compiles. The harness sets `VK_ADD_LAYER_PATH` itself so validation layers load.

The harness also sets `SS_VALIDATION_NONFATAL=1`: by default the Vulkan validation messenger
asserts (and the process dies) on the first ERROR, so you only see one error per run. With this env
var set, every validation error is logged and the app keeps running, so a single smoke run surfaces
**all** of them at once — the harness then detects failures by scanning the log, not the exit code.
Set it yourself when debugging validation interactively. GPU resources are also named via
`SetVulkanObjectName` (`VK_EXT_debug_utils`), so validation/RenderDoc report e.g. `Swapchain[0]`
instead of a raw `VkImage 0x...` handle.

## Console variables (CVars)

Engine flags go through a small CVar registry (`Snowstorm/Utility/CVar.hpp`) instead of ad-hoc
`std::getenv`. Declare engine-wide CVars in `Snowstorm/Core/EngineCVars.{hpp,cpp}`; each
self-registers and is resolved once at startup by `CVarRegistry::Initialize(argc, argv)` (called in
`EntryPoint.hpp`) from, in increasing priority: **default → environment → CLI**.

A CVar named `validation.extra` is set by env `SS_VALIDATION_EXTRA` **or** CLI `--validation.extra`
(dots→`_`, uppercased, `SS_` prefix for env). Bools accept presence (`--flag`, or env set to
anything but `0`/`false`/`off`/`no`). Run any executable with `--list-cvars` (or `--help`) to print
every CVar with its value, type, env name, and description. Current CVars: `smoke.frames`,
`validation.nonfatal`, `validation.extra` (the smoke harness still sets the matching env vars, so
nothing about running it changed). Resolution is read-once at startup — runtime mutation (ImGui
panel) and a config-file source are planned follow-ups, not yet implemented.

## Layout

```
Snowstorm-Core/      # STATIC library: all engine code (the only place most work happens)
  Source/Snowstorm/  #   platform-independent engine (Core, ECS, Render, Systems, ...)
  Source/Platform/   #   Vulkan/ (RHI implementation, ~28 files) and Windows/
Snowstorm-Editor/    # Editor EXECUTABLE — links Core; ImGui dockspace, panels, viewport
Snowstorm-Runtime/   # Editor-free runtime EXECUTABLE — links Core; shares RegisterCoreSystems (WIP)
Assets/              # Shaders, Meshes, Materials, Scenes, Textures (loaded at runtime)
Scripts/             # Generate-Solution.{py,bat}
Tools/dxc/           # DirectX Shader Compiler (HLSL -> SPIR-V)
```

Core builds to a static lib holding code shared by multiple apps; executables (currently the Editor)
link Core and add it to their include path. All targets are **C++20** (the root `CMakeLists.txt`
sets C++17 globally, but every target overrides to 20 — treat the project as C++20).

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
  `SS_DEBUGBREAK()`. Logging is `SS_CORE_*` / `SS_*` (spdlog). Asserts compile out unless `SS_DEBUG`;
  use `SS_VERIFY` / `SS_CORE_VERIFY` for checks that must survive release builds.
- Platform code goes behind `SS_PLATFORM_WINDOWS` (see `Core/PlatformDetection.hpp`); the engine
  currently `#error`s on non-Windows.
- Headers are `.hpp`, translation units `.cpp`. Core globs all sources recursively, so a new file
  under `Snowstorm-Core/Source/` is picked up after re-running CMake (re-generate the solution).
- **Formatting (format-on-touch):** the repo has a `.clang-format`. The `lint` CI checks the C++
  files changed by a push/PR and **fails if any touched file isn't fully clang-format-clean**, so the
  codebase formats gradually as files are edited. Pinned to **`clang-format==22.1.5`** (match it
  locally — version drift changes output). Run `clang-format -i <files>` (or enable format-on-save
  against the repo config) before committing.

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
- After non-trivial runtime changes, **build then run `Scripts/smoke-test.py`** (see Smoke test
  above) — it catches crashes, hangs, and Vulkan validation/assertion errors that compilation can't.
  A clean smoke run is the minimum bar before claiming a runtime change works.
- Confirm behavior against the actual source/build, not from names. Mark unverified statements as
  assumptions.

### Build verification (learned the hard way)

- **Check the build exit code, not a grepped log.** `cmake --build ... | grep -i error` can miss the
  real failure (MSBuild error formatting varies) and report success on a broken build. Always inspect
  `${PIPESTATUS[0]}` / the actual exit status. A failed compile leaves the **previous** exe in place,
  so the app keeps running stale code and every downstream test is meaningless.
- **Confirm the exe was actually rebuilt** before testing behavior: check the binary's timestamp
  (`ls -l build/Snowstorm-Editor/Debug/Snowstorm-Editor.exe`) is newer than your edit. If a "rebuild"
  didn't update the timestamp, the build failed silently — fix that first. This is the #1 cause of
  "my change isn't taking effect."
- **A running editor locks the exe.** `LNK1168: cannot open ... for writing` means a previous instance
  is still alive; `taskkill //IM Snowstorm-Editor.exe //F` before rebuilding. A leftover process also
  means you may be looking at an old build.
- Strip all temporary debug probes (logs, on-screen text) before committing, and `git diff` each
  touched file to catch leftovers — incremental edits during debugging are easy to forget.

### Don't turn the user into your debugger

- Prefer verification you control: headless runs (`SS_SMOKE_FRAMES=N`), startup-time logging, and
  reading source/state. Reserve "please click X and tell me what you see" for genuine final visual
  confirmation, not for diagnosing logic — a manual launch→click→report loop burns the user's time
  and stalls on build/timing artifacts.
- **Keep effort proportional.** Time-box cosmetic/nice-to-have features; if one can't be made to work
  and verified in a couple of clean attempts, drop it rather than rabbit-holing. Commit the larger
  body of working, verified changes promptly instead of leaving it uncommitted while chasing a detail.

### How to debug effectively (don't guess in a loop)

- **Bisect, don't guess.** When behavior contradicts the code, the bug is somewhere between "what I
  believe is true" and "what's observed." Add a probe that splits that gap in half and *prove* which
  side is wrong, rather than changing code speculatively and re-running. One well-placed probe beats
  five hopeful edits.
- **One assumption per probe; isolate the variable.** Each test should answer exactly one yes/no
  question. If a result is paradoxical (e.g. "metadata valid at registration but absent at render"),
  do not theorize further — put *both* readings in a *single build/run* and compare. Contradictions
  across separate runs usually mean the runs differed (stale exe, different selection), not that the
  code is haunted.
- **Verify the harness before the hypothesis.** Before concluding "the code is wrong," confirm the
  test itself is valid: right binary (timestamp), build actually succeeded, the probe code path is
  even reached. Most "impossible" bugs are a broken test, not broken code.
- **Make the probe observable without a human.** Favor startup-time logs and `SS_SMOKE_FRAMES` runs
  whose output you can read directly. An on-screen-only probe that requires a click is a last resort
  and is itself a debuggability smell (see below).
- **When stuck after ~2 failed attempts, change altitude.** Stop poking the same spot: re-read the
  full function (not a snippet), question the premise, or check the layer above/below (build system,
  RTTR registration, ImGui ID/widget state). Repeating a variant of a failed approach is the signal
  to step back, not to try harder.

### Build the engine to be debuggable

The deeper fix for "I couldn't verify without the user" is to make state inspectable in code:

- **Expose state to headless inspection.** If you can only confirm a feature by looking at the screen,
  add a non-visual path to read the same truth: a startup/CVar-gated dump, a query function, or a log.
  The inspector's reflection (RTTR) and the `smoke.frames` hook already make a lot of state reachable
  without a GPU — prefer wiring new state through those.
- **Prefer pure, testable cores.** Logic that maps data→data (name formatting, layout math, value
  conversions, asset-handle resolution) should live in free functions that a Catch2 test or a headless
  run can exercise directly — not be entangled in an ImGui draw call that only runs on a click.
- **Fail loud, not silent.** Silent fallbacks (a missing asset resolving to null, an unread metadata
  key, a default value) hide bugs and force interactive spelunking. Log once at `[error]`/`[warn]`
  when an expectation is violated, the way `ResolveAssetName` / the unresolved-handle path do.
- **Name things for diagnosis.** Vulkan objects via `SetVulkanObjectName`, ImGui widgets with stable
  unique IDs, components with reflected type names — so logs, validation, and RenderDoc say
  `Swapchain[0]` / `DIRECTIONAL LIGHT`, not an opaque handle.
- **Treat "I had to add a temporary on-screen probe" as a missing feature.** It usually means that
  state should be permanently visible (a debug overlay / stats panel / CVar dump). Consider promoting
  the probe into a real, toggleable diagnostic instead of deleting it.
