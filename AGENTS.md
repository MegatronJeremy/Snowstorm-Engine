# AGENTS.md: Snowstorm Engine

3D game engine with an abstraction over the rendering backend. Currently Vulkan-only (DirectX 12
planned). Windows-only for now. Public domain (`UNLICENSE.txt`). The engine is Hazel-inspired
(`Ref`/`Scope`, `Layer`/`LayerStack`, `SS_*` macros, instrumentation profiler) but has grown its
own EnTT-based ECS, a Systems/Singletons/Services architecture, a Vulkan RHI, an asset system, and
an ImGui editor.

> This repo is consumed as a git **submodule** of `MegatronJeremy/RG2`, but it has its own
> independent history and `master` branch. Develop it as a standalone project. The working language
> here is **English** for everything: code, comments, commit messages, issues, and chat. (The RG2
> parent asks for Serbian commits and chat; that governs RG2's own contents, not this repo.)

## Agent collaboration

How to work in this repo. These rules live here rather than in a personal config so that a clone on
any machine, driven by any agent, behaves the same.

- **Every change goes on a feature branch and reaches `master` through a pull request.** Never commit
  to `master` directly and never push to it, including a fast-forward of work that was already
  reviewed in chat: the PR is the durable record of what changed and why, and it is where CI actually
  runs the gates (lint, tests, shader occupancy). Branch names are `feat/<topic>`, `fix/<topic>` or
  `chore/<topic>`. Open the PR with `gh pr create`, let the checks finish, and leave the merge itself
  to a human. A PR whose branch has diverged gets `master` merged INTO it and the conflicts resolved
  on the branch, never the reverse. The pre-push hook refuses a direct push to `master`, so this is
  enforced rather than remembered, but the hook is opt-in per clone
  (`git config core.hooksPath .githooks`) and the rule holds whether or not it is armed.
- **Review in chat before push, and push is gated on explicit approval.** For every change, show the
  diff or the key snippets with enough surrounding context to read like a real PR: what changed, why
  it was done that way, the tradeoffs, non-obvious invariants, and what remains unverified. A local
  commit before that review is fine; the gate is on `git push`. The same gate applies to anything
  else outward-facing or hard to reverse (issue comments, remote branches, force pushes).
- **One increment per step.** Each step leaves the repo building and runnable. Small reviewable
  changes beat big batches.
- **A change to a showcased feature updates its page.** `docs/showcase/` pages carry measured numbers and images for a feature; the PR that changes the feature updates the page, its numbers (GPU, commit, date) and its images in the same PR.
- **Prefer a short plan before touching a risky surface**: descriptor/binding layouts, serialization
  formats, shared or global state, public APIs. Two minutes of planning catches the class of bug that
  otherwise only surfaces after a full build and test cycle.
- **Verify NUMBERS, not just actions.** Never state a measurement, benchmark, or quantitative result
  that was not actually produced. If a measurement is unfinished, say so; a fabricated number is
  worse than "not measured yet". Every summary separates what was built, run, and measured from what
  is inference, and marks the inference as such.
- **Flag conflicts of interest.** When reviewing work you produced earlier (your own code against an
  external PR, your own benchmark against someone's claim), say so, so the verdict can be weighted.
- **Assume deep fundamentals.** The reader is a graphics/systems engineer, so skip tutorial framing
  on C++, Vulkan, and rendering basics. Terse and correct beats verbose and hedged.

### Writing: comments, docs, commits, issues

- **Maximal information, minimal text.** Cut every word that adds none: no throat-clearing, no
  filler, no restating the obvious. Keep the quantitative (numbers, names, counts) and the
  qualitative (why, tradeoff), and cut the connective overhead between them.
- **Committed artifacts state durable facts, not status.** No work log, no TODO, no "as of this
  change" framing. Ephemeral status belongs in the issue tracker, where it is meant to change; in a
  file it rots and forces a churn commit to correct it.
- **Comments: default to none.** Names carry WHAT. Comment only a non-obvious WHY that the code
  cannot express: a hidden constraint, a load-bearing invariant or ordering, a workaround, or domain
  encoding a reader cannot derive from the code (wire-format bit ops, protocol field numbers, bit
  layouts). Cold-reader test: would a competent reader who does not already hold this domain in their
  head be lost here? Prefer a structural fix (named constant, better name, small helper) over a
  comment. Never restate the signature, narrate the line, or reference a ticket or a caller.
- **No em-dashes, anywhere.** The glyph is not the whole problem: the tell is clauses joined by
  adjacency instead of by a stated relationship, so deleting the character alone relocates the habit
  into comma splices and stacked parentheticals. Name the relationship instead. Use a colon if the
  second half explains the first, "although" or "since" if it qualifies, parentheses for a genuine
  aside, a semicolon between two independent clauses. A plain hyphen dash is fine.
- **Revise, do not just constrain.** Run a second pass over your own draft: with the whole clause in
  context the connective can match how the sentence actually ended, which an upfront ban cannot do.
  Strip the other AI-writing tells on that pass (the "not just X, but Y" antithesis, rule-of-three
  lists, hollow puffery).
- **Keep contributor workflow out of consumer-facing docs.** `README.md` is for someone using the
  engine; build, test, and release process lives next to the code it concerns.

## Where the rest lives

Area guidance lives next to the code it concerns. These files are part of the agent instructions: read the one for the area you touch before changing it, and a change that alters what it says updates it in the same PR.

| Touching | Read |
|---|---|
| a test or gate, perf or quality measurement, tuning, the benchmark route | [`Scripts/README.md`](Scripts/README.md) |
| the renderer: descriptor sets, texture cook, shadows, alpha mode, 2D, shader hot-reload, IBL, tuning CVars | [`Snowstorm-Core/Source/Snowstorm/Render/README.md`](Snowstorm-Core/Source/Snowstorm/Render/README.md) |
| audio | [`Snowstorm-Core/Source/Snowstorm/Audio/README.md`](Snowstorm-Core/Source/Snowstorm/Audio/README.md) |
| `Games/`, or how a game links and registers | [`Games/README.md`](Games/README.md) |
| editor play modes, project selection | [`Snowstorm-Editor/README.md`](Snowstorm-Editor/README.md) |

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

**MSVC toolset is detected, not hardcoded.** vcpkg and CMake pick a toolset by different rules
(vcpkg takes the latest installed minor version; a bare `-T v143` takes the VS instance default),
and when they diverge Catch2 fails to link with LNK2019 on vectorized-STL symbols. The script
resolves the latest installed toolset once via `vswhere` and gives the same version to both: CMake
via `-T v143,version=<ver>`, vcpkg via the overlay triplet in `Scripts/vcpkg-triplets/` (a copy of
stock `x64-windows` plus `VCPKG_PLATFORM_TOOLSET_VERSION`, read from `SS_MSVC_TOOLSET_VERSION`). It
also pins `CMAKE_GENERATOR_INSTANCE` so a multi-VS box can't resolve the version against a different
install. Override with `SS_MSVC_TOOLSET=<ver>`; detection failure falls back to a bare `-T v143`
with a warning. Because the triplet feeds vcpkg's ABI hash, **changing the resolved toolset
invalidates installed packages**: re-run with `--fresh` when a toolset change forces a rebuild.

Then open `build/Snowstorm.sln` and build. **Snowstorm-Editor** is the default startup project;
the debugger working directory is the repo root, so relative `Engine/...` and `Projects/...` paths
resolve. Vulkan
validation layers are wired via the `VK_ADD_LAYER_PATH` env var (set by the script and in the VS
debugger environment) pointing at the vcpkg `bin` dir.

The first run is slow: vcpkg compiles every dependency from source.

### New box (one time)

A clone plus these commands reproduces the full workflow. There is deliberately no setup script:
this is six commands, documented in this section and in [`Scripts/README.md`](Scripts/README.md).

```
git clone https://github.com/MegatronJeremy/Snowstorm-Engine && cd Snowstorm-Engine
git config core.hooksPath .githooks              # arms the pre-push hook: format check + no direct master pushes
pip install clang-format==22.1.5 numpy flip-evaluator==1.7
py Scripts/Generate-Solution.py                  # bootstraps vcpkg; the first run compiles every dependency
cmake --build build --config Debug
py Scripts/smoke-test.py
```

Requires Windows and Visual Studio 2022 (`Generate-Solution.py` detects the exact `v143` toolset
version). No Vulkan SDK installation is needed: loader, headers, and validation layers all come from
vcpkg. `Generate-Solution.py` runs `vcpkg integrate install`, a machine-global MSBuild side effect
outside the repo.

The Python packages serve the gates, not the engine: `clang-format` at the pinned version for the
pre-push lint hook, `numpy` as a hard requirement of `quality-bench.py`, `flip-evaluator` for the
perceptual metric (optional, although its absence is now reported rather than silently skipped).

**The golden-file gates start empty on a new machine.** Perf and RGA baselines are keyed by adapter
and ASIC, and quality baselines are captured against a path trace on the local GPU, so none of the
committed sets apply until this box captures its own:

```
py Scripts/perf-bench.py --update-baseline       # only if this adapter has no committed set
py Scripts/cook-shaders.py --variants rt,base    # both permutations, matching what CI cooks
py Scripts/rga-occupancy.py --spv-dir Engine/cache/shaders-cook
```

Until a baseline exists, those gates exit **2** (SKIP), never 0, so an unmeasured machine cannot
report a pass. Commit a new adapter's perf and RGA baselines. Quality baselines are the exception:
they are committed but not device-keyed by path, so on a different GPU the gate reports SKIP and
`--update-baseline` would overwrite the existing machine's set. Re-capture locally, keep it out of
the commit, or move the reference machine deliberately.

## Staging a runnable build

`cmake --build build --config Debug --target stage` produces `build/stage/<config>/`: a directory the
executables run out of with no engine tree present.

```
cmake --build build --config Debug --target stage
cd build/stage/Debug && ./Snowstorm-Pong.exe --startup.scene=Projects/Sandbox/assets/scenes/Pong.world
```

A GAME adds its own executable and project through two functions the engine exposes, which is what
makes `stage` package a game rather than only this repository's sample:

```cmake
snowstorm_stage_executable(Snowstorm-Doom)
snowstorm_stage_project("${CMAKE_CURRENT_SOURCE_DIR}/Projects/Doom")
```

Functions rather than a list the engine reads, because a consuming game's targets and project only
exist AFTER `add_subdirectory(Snowstorm-Engine)`, so nothing evaluated there could name them. This
repository declares its own contributions the same way, guarded on being the top-level project so a
game packaging itself does not find the engine's editor, Pong and 54 MB sample project in its build.

The stage is added to, never pruned (`copy_directory_if_different` does not remove), so content deleted
from the source lingers until `build/stage` is deleted.

**Cooking has an editor surface, packaging deliberately does not.** *File > Cook Assets* runs
`CookAllRegistryAssets`, the same function `--cook.assets` runs, so the two cannot disagree. Producing a
stage stays a CMake target: it copies build output, so a button for it would have to guess which build
directory produced the running executable and shell out to a generator that may not be installed.
Unreal can offer *File > Package Project* because UAT is a first-class shipped tool; nothing here is.

**Gate the package, not just the build**: `py Scripts/smoke-test.py --staged` runs the executables out
of the stage with the stage as the working directory. It is the only check that the PACKAGE works. The
bug that motivated it built, linked and passed every other gate: stale stamps left a stage with an empty
`Projects/`, and it failed only when run from there. Verified to catch exactly that, by removing
`Projects/` from a stage and watching the gate go FAIL.

**Not part of the default build, on purpose.** The payload is ~140 MB and packaging is a step you ask
for, the way Unreal stages on package rather than on compile. It also leaves every existing path alone,
so smoke, perf and quality keep finding executables where they always have.

**What makes it self-contained is `GetEngineRoot`'s rule 2**: it walks up from the executable for
`Engine/Shaders`, finds it in the stage, and never consults the source tree. That is why the layout
mirrors the repo instead of flattening it, and it is what retires rule 3 (the baked configure-time
path) for a shipped build. Verified rather than assumed: a staged run writes its cache into the STAGE's
`Engine/cache` and leaves the source tree's untouched, and since that path is `GetEngineRoot()/Engine/
cache`, it is proof of which root won.

`Engine/cache` is deliberately NOT staged: it is generated, and the stage must stay writable so a cold
run populates it in place.

**Each executable's whole output directory is copied**, not the exe plus `$<TARGET_RUNTIME_DLLS>`. That
generator expression names only DLLs of directly linked imported targets and misses assimp's transitive
dependencies (minizip, pugixml, poly2tri, zlib), which vcpkg's applocal deployment is what actually puts
next to the exe. Staging 7 of 18 DLLs produced a stage whose executables would not start.

The content copy hangs off a stamp file rather than a `POST_BUILD` command, because `POST_BUILD` only
fires when its target RELINKS: editing a scene and rebuilding would stage the old one.

## Console variables (CVars)

Engine flags go through a small CVar registry (`Snowstorm/Utility/CVar.hpp`) instead of ad-hoc
`std::getenv`. Declare engine-wide CVars in `Snowstorm/Core/EngineCVars.{hpp,cpp}`; each
self-registers and is resolved once at startup by `CVarRegistry::Initialize(argc, argv)` (called in
`EntryPoint.hpp`) from, in increasing priority: **default → `<project>/Saved/Settings.cfg` →
`SnowstormStartup.cfg` → environment → CLI**. The project settings file is per project (a switch
never clobbers another project's tuning), gitignored, auto-saved by the editor on shutdown and holds
persistent CVars only. It loads once the project is active (`LoadProjectSettings`), not in
`Initialize`, because `startup.project` is itself a CVar; CVars set by the startup file, env or CLI
are skipped by that load so the order above holds. A project without one migrates once from the
legacy root `SnowstormConfig.cfg`. `SnowstormStartup.cfg` is hand-authored, never written by
the app, and can carry any CVar, so it is the safe place for machine-local toggles the auto-save
cannot clobber. `config.ignore` (`SS_CONFIG_IGNORE=1`) skips both files, which is what the benchmark
harnesses set so a run depends on code defaults plus its own overrides, never on local settings.

A CVar named `validation.extra` is set by env `SS_VALIDATION_EXTRA` **or** CLI `--validation.extra`
(dots→`_`, uppercased, `SS_` prefix for env). Bools accept presence (`--flag`, or env set to
anything but `0`/`false`/`off`/`no`). Run any executable with `--list-cvars` (or `--help`) to print
every CVar with its value, type, env name, and description. `EngineCVars.cpp` declares them
(rendering technique modes, denoiser knobs, path tracer, benchmark hooks, validation); treat
`--list-cvars` as the authoritative list rather than any enumeration here. Startup resolution runs
once, but CVars can also be **edited live at runtime** from the editor's *Debug > Console Variables*
panel (`CVarPanelSystem`): it lists every CVar with a type-appropriate widget (checkbox/int/float)
plus a `name value` command line, via typed accessors on `ICVar` (`GetKind`/`Get*`/`Set*`). Most
engine CVars are read per-frame through `.Get()` (shadows, IBL, exposure, shadow quality), so edits
take effect immediately, and those marked `CVarFlags::Persist` are written back to
`<project>/Saved/Settings.cfg` on shutdown.

## Layout

```
Snowstorm-Core/      # STATIC library: all engine code (the only place most work happens)
  Source/Snowstorm/  #   platform-independent engine (Core, ECS, Render, Systems, ...)
  Source/Platform/   #   Vulkan/ (RHI implementation, ~28 files) and Windows/
Snowstorm-Editor/    # Editor EXECUTABLE, links Core; ImGui dockspace, panels, viewport
Snowstorm-Runtime/   # Editor-free player EXECUTABLE; a ~25-line shell over GameLayer
Snowstorm-Tests/     # Catch2 unit tests (GPU-free; run by ctest, gated in CI)
Games/Pong/          # the small example GAME: what building on the engine actually takes, ~300 lines
Engine/              # engine-owned runtime data: Shaders/, Fonts/, and the gitignored cache/
Projects/Sandbox/    # the sample project: assets/ (scenes, meshes, materials, textures, registry)
Dataset/             # gitignored capture output + trained weights
Scripts/             # build, smoke, perf, quality, shader-occupancy tooling + committed baselines
Tools/dxc/           # DirectX Shader Compiler (HLSL -> SPIR-V)
```

Core builds to a static lib holding code shared by multiple apps; executables (currently the Editor)
link Core and add it to their include path. All targets are **C++20** (the root `CMakeLists.txt`
sets C++17 globally, although every target overrides to 20, so treat the project as C++20).

**Keep the in-editor shortcut reference current.** The editor has a *Help > Keyboard & Mouse
Shortcuts* window (`EditorMenuSystem::DrawShortcutsWindow`) that documents every keyboard/mouse
binding. It draws the data table in `Snowstorm-Editor/Source/Singletons/ShortcutTable.cpp`.
Whenever you add, remove, or change a shortcut (camera controls, gizmo keys, framing, save,
selection, hierarchy actions, …), edit that table in the same change; `ShortcutTableTests` pins
unique chords per section and the presence of the bindings the editor handles. It is the single source of truth users
see, so treat it as part of the feature, not an afterthought.

## Architecture (Core)

- **Game bootstrap:** `Core/GameLayer.hpp` is the layer a non-editor host pushes. It builds the `World`,
  boots the active project, calls `RegisterCoreSystems`, loads the startup scene, and makes the viewport
  and camera. A game passes its own `void(World&)` registration to the constructor, which runs
  immediately after `RegisterCoreSystems` so its systems land behind every engine system in the same
  phase. It is engine code rather than one executable's private layer because every shipping game needs
  exactly this bootstrap; `Snowstorm-Runtime` and `Snowstorm-Doom` are both thin shells over it.
- **Entry point:** clients define `Snowstorm::CreateApplication()`; `Core/EntryPoint.hpp` provides
  `main` (inits logging, wraps `Run()` in profiler sessions). `Application` owns the window, the
  `LayerStack`, the `EventBus`, and the `ServiceManager` (singleton via `Application::Get()`).
- **ECS:** EnTT-backed. `World`/`Entity` (`World/`), components in `Components/`, behavior in
  `Systems/` (managed by `SystemManager`), cross-cutting state in `Singletons/` (`SingletonManager`),
  and `Service/` for longer-lived services. Components self-register for reflection via **RTTR** plus
  the editor/serializer registry from a per-component static initializer (`RTTR_REGISTRATION { ... }` +
  `AUTO_REGISTER_COMPONENT(T)` in each `Components/*.cpp`; see `Components/ComponentRegistry.hpp`).
  Core is a static lib, so the executables link it `WHOLE_ARCHIVE` to keep those initializer TUs.
- **Data-parallelism is a first-class option for systems: always consider it.** When adding a new
  system (or extending/reworking one), explicitly ask whether its per-entity work is *pure and
  independent* (each entity reads/writes only its OWN components, no shared accumulator, no renderer/
  asset-manager/singleton calls, no `TrackedRegistry` mutation APIs in the loop) and would benefit from
  running across `JobSystem` workers. If so, use the existing primitives instead of a hand-rolled serial
  loop: `System::ParallelForEach<Read<T>/Write<T>...>` for in-place per-entity updates (the DOTS
  IJobEntity model; RotatorSystem is the reference), or `JobSystem::ParallelGather<T>(count, body, emit)`
  for parallel filter/collect into a list (VisibilitySystem's frustum cull is the reference). Both take
  a grain size, degrade to an inline serial pass for small N (parallel only when it pays), gate on the
  `ecs.parallel` CVar for a pure serial-vs-parallel A/B, and preserve deterministic (bit-identical)
  output so `ChangedView`/draw order stay stable. Most systems will NOT qualify (they submit to the
  renderer, touch singletons, run scripts, or scatter into shared state), and those stay serial on plain
  `System`, and that's the correct call, not a missed optimization. The point is to make the
  parallel-vs-serial decision *consciously* each time, not default to serial by habit. Note the current
  ceiling: the O(n) post-barrier change-mark (#91) caps end-to-end speedup at scale even when the
  compute parallelizes near-linearly, so measure with `--ecs.benchmark` rather than assuming a win.
- **System access declarations:** every core system overrides `System::DeclareAccess(SystemAccess&)` with the
  components and resources (Singletons, Services, and the `TrackingMapResource` pseudo-resource for tracked
  mutators and `Init/Changed/FiniView`) it reads and writes, plus `Structural()` when it emplaces, removes or
  destroys. Undeclared means exclusive and main-thread-only. `SystemManager::DeclaredAccess()` lists them and
  `SystemAccess::ConflictsWith` is the pairwise rule, and `BuildSchedule` (`ECS/SystemSchedule.hpp`) layers one phase into stages from it; `ecs.schedule` (default off) makes
  `SystemManager` run each phase in `ScheduleOrder` (stage by stage, still serial, edit-mode skips applied before
  staging); nothing runs in parallel yet (#85). A new core system needs a declaration (`SystemAccessTests` fails otherwise).
- **Cluster grid math:** `Render/LightClusterMath.hpp` is the pure, GPU-free core of the planned clustered light cull (#66): exponential depth slices, analytic view-space cluster bounds, sphere-AABB overlap and a CPU reference cull (ascending index, fixed stride, overflow dropped). Nothing renders from it yet.
`AssignClusterLights` builds fixed-stride per-cluster lists from it; `render.lights.cluster` (default off) runs it per frame for the presented view (point and spot range spheres) and publishes `RendererService::GetClusterStats()`. `render.lights.cluster.shade` (default off, implies the assignment) uploads the lists (`PackClusterLightLists`: counts, then fixed-stride indices) to a per-frame-in-flight storage buffer that DefaultLit reads through its device address (`vk::RawBufferLoad`, no descriptor change; address, grid and `ClusterViewDepthRow` ride a FrameCB tail). DefaultLit visits only the cluster's point and spot lights, in the same ascending order as the full loops, so pixels match unless a cluster overflows `render.lights.cluster.max`. Only the presented camera at a scene-target size equal to the viewport uses the lists (`render.scale != 1` and every other pass loop over all lights); `ClusterIndexFromPixel` is the tested mirror of the shader's cluster lookup. Not yet measured on a GPU.
- **Rendering:** backend-agnostic interfaces in `Render/` (`RendererAPI`, `Renderer`, `Pipeline`,
  `Shader`, `Buffer`, `Texture`, `Material`, `RenderGraph`, ...). The concrete implementation lives
  in `Platform/Vulkan/` (volk + Vulkan Memory Allocator + spirv-reflect; shaders compiled to SPIR-V
  via `Tools/dxc`).
- **Scenes:** serialized to/from JSON (`World/SceneSerializer.hpp`, nlohmann_json).
  Every component serializes through its reflected properties (`JsonUtils.hpp`); the serializer holds no
  type names. A shape that is not the property list sets `SerializeFn`/`DeserializeFn` in
  `ComponentRegisterOptions` (`MaterialOverridesComponent`), `Structural` keeps a component in the entity
  header (`IDComponent`, `TagComponent`), and property metadata `JsonAlias` names the key older files used.
- **Render graph scheduling is CPU-testable:** `RenderGraph::BuildDependencies` and `BuildSchedule(swapchain)` need no
  device (`RenderGraphScheduleTests`); only `Execute` (fork/join, ownership transfers) needs a GPU.
- **Events:** `Events/` hierarchy dispatched through `EventBus`; input bridged in `Input/`.

### Conventions

- Namespace `Snowstorm`. Smart-pointer aliases `Ref<T>` (shared) / `Scope<T>` (unique) with
  `CreateRef` / `CreateScope`. Use these, not raw `std::shared_ptr`/`make_unique`, in engine code.
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
  locally, since version drift changes output). Run `clang-format -i <files>` (or enable format-on-save
  against the repo config) before committing. `Scripts/check-format.py` predicts this gate locally:
  default mode checks the files changed vs `master` + uncommitted (the same set CI gates on), `--all`
  scans the whole project (surfaces the legacy backlog CI does *not* gate on), `--fix` reformats in
  place. A tracked `pre-push` hook (`.githooks/pre-push`) runs the default mode so a lint-failing push
  is blocked before it leaves the machine. **Enable it once per clone** with
  `git config core.hooksPath .githooks` (bypass a single push with `git push --no-verify`).
- **Shared-header shader bindings are global: mind `space1` collisions (learned from #60).** A
  resource declared in `Engine/Shaders/Include/Engine.hlsli` is emitted into *every* shader that
  includes it, and with `-fspv-preserve-bindings` (always on) it survives even when unused, so it
  lands in the reflected layout of every pipeline, including the full-screen post passes
  (Fxaa/Sharpen/TemporalResolve), which pair their frag with `Fullscreen.vert` (also includes the
  header) and **park their own cbuffers/textures high in `space1` (bindings 3/4/5/6) to dodge the
  material bindings 0/1/2**. Adding a new binding to the shared header at one of those slots silently
  collides with a post pass's resource of a *different* descriptor type in the same pipeline (a
  validation error, not a compile error: it only shows in smoke). If a binding is used by only one
  shader family (e.g. the shadow comparison sampler is DefaultLit-only), declare it in that shader's
  `.frag`, NOT the shared header, and gate any C++ that binds it on the reflected layout actually
  having that binding (custom-shader materials like Mandelbrot won't).

## Function complexity

- A change does not raise a function above cognitive complexity 25, or raise one that is already
  above it, as clang-tidy's `readability-function-cognitive-complexity` measures it.
  `py Scripts/complexity-check.py` checks it (exit 0 pass, 1 fail, 2 skip). Tests and vendored code
  are exempt.
- Lower it by removing what makes code hard to follow: nesting (return early) and repeated
  skeletons (one shared helper where the same loop or parse appears more than once). Do not split a
  function into helpers that are each called once just to meet the number. The logic then reads
  across several functions instead of one, which is harder to follow, not easier.
- Functions over the limit when the rule came in are listed in `Scripts/complexity-baseline.json`
  at their score then. They are left alone unless a change has another reason to touch them, since
  refactoring for the number alone churns code without making it simpler. One brought back under
  the limit leaves the baseline in the same PR.

## Dependencies (vcpkg, x64-windows)

assimp, EnTT, fmt, glew, glfw3, glm, imgui (vulkan+glfw bindings, docking), imguizmo, rttr, spdlog,
stb, Vulkan SDK, vulkan-memory-allocator, volk, spirv-reflect, nlohmann-json, catch2, tracy, libsquish,
miniaudio. The canonical list
is `PACKAGES` in `Scripts/Generate-Solution.py`; the linkage is in `Snowstorm-Core/CMakeLists.txt`.
Keep those two in sync when adding a dependency.

## Git hygiene

Work on a branch, merge through a pull request; see the branch/PR rule under Agent collaboration.
`.gitignore` excludes everything generated: `build/`, `vcpkg/`, `.vs/`, `Engine/cache`, the local
config files (`Projects/*/Saved/`, `SnowstormConfig.cfg`, `SnowstormStartup.cfg`), captures and weights (`Dataset/`,
`*.npy`, `*.ssnn`), downloaded tooling (`Tools/rga/`, `Tools/rdts/`), the PT reference cache
(`Scripts/.quality-ref-cache/`), and all solution/project files (`*.sln`, `*.vcxproj*`, `*.cmake`,
`CMakeCache.txt`, `ALL_BUILD.*`, `ZERO_CHECK.*`, `Makefile`). The committed baselines under
`Scripts/{perf,rga,quality}-baseline/*.json` are re-included explicitly, so they survive those rules.
Never commit generated or compiled artifacts. Commit messages in English.

## Think like a real engine

**Always** check how a serious production engine (Unreal, Unity, Godot, modern in-house) does it
*before* proposing or implementing any design. This is a required step, not an optional prompt.
Name the reference model concretely (e.g. "Unity Clear Flags", "Unreal SkyAtmosphere actor", "Godot
WorldEnvironment Background Mode"), state how that engine actually structures the feature, and only
then deliberately decide how far to go for *this* project. If you're unsure how the reference engines
do it, research it (web search / docs) rather than guessing: a vague "engines usually…" is not
acceptable. The point is to anchor every design decision in a proven pattern so today's choice is a
known subset of the real thing, not an accidental invention.

**Lead with the more rigid, long-term-correct option.** When choosing between a quick patch and the
structurally sound design, *propose the sound one first* and recommend it by default (even if it is
more work), and only fall back to the shortcut when there is a concrete reason (time-box, throwaway
code, the right design needs infra that doesn't exist yet). Don't offer the lazy option as the
headline and the good one as an afterthought. Vuk's stated preference: this should feel like a
professional engine, so bias toward the design that a production codebase would actually ship. A
worked example: when per-entity material overrides needed an editor, the rigid choice was to replace
the fixed `mask + one-field-per-property` struct with a *sparse list of named, typed overrides*
(Unity `MaterialPropertyBlock` / Unreal MID) rather than just bolting a picker onto the old shape:
the latter would have had to be ripped out the moment a third override type appeared. The point is not
to build AAA infrastructure (this is a thesis platform), but to make the simplification a *conscious* choice with the real shape
in view, so today's shortcut is a known subset of the right design rather than an accidental dead
end. Call out which parts are intentionally deferred and why, and prefer shortcuts that are a
*smaller version of* the real thing (so they extend later) over ones that would have to be ripped
out. When the "real" way is genuinely cheap, just do it the real way.

**Counterweight, and actively guard against bloat.** "Long-term-correct" is NOT "more layers." Before
adding any new abstraction, base class, wrapper, config knob, or indirection, ask out loud: *does this
earn its keep, or is it speculative?* An abstraction with one caller/subclass, a wrapper that only
saves a few lines, a second way to do something the codebase already does: these are bloat, not
rigor. Prefer the load-bearing primitive over sugar layered on top of it; prefer one clear way to do a
thing over two. On every new feature/implementation, explicitly weigh whether it *adds* surface area
(a concept a future reader must learn, a decision they must make) against what it removes, and say so.
When a proposed piece optimizes the rare case while taxing the common case, that's backwards, so cut it.
The bias toward the production-grade design (above) and the bias against bloat are the same instinct:
build the real shape, but only the parts that are actually load-bearing. When in doubt, leave it out:
re-adding a thin wrapper later is cheap; ripping out an entangled one that grew callers is not. A
worked example: a CRTP `EntitySystem` base was built to wrap the `ParallelForEach` primitive for
single-query systems, then cut. It had one subclass, only fit the one-query case (multi-loop systems
drop back to the primitive anyway), and added a "which base do I derive?" decision to every new
system, all to save ~5 lines. The primitive was the real abstraction; the wrapper was bloat.

Worked example, the **asset pipeline** (the engine's biggest deliberate simplification):

- **Real engines separate source assets from cooked runtime assets.** The file you drop in
  (`.obj`/`.png`/`.fbx`) is the *source*; an *importer* cooks it once into a GPU-ready artifact
  (mesh → packed vertex/index buffers; texture → BC7/ASTC + mips; shader → SPIR-V/DXIL) plus a
  sidecar `.meta` holding a stable GUID + import settings (cf. Unity's `foo.fbx.meta`). Scenes
  reference the **GUID**, never the path, so moving/renaming a file never breaks references.
- An **asset database** maps `GUID → (source, cooked, dependencies, content hash)`; a **file
  watcher** re-cooks only what changed (and its dependents) and hot-reloads it; the runtime
  **streams** cooked assets asynchronously under a memory budget; builds cook only the transitive
  closure of what scenes actually reference (no dead content shipped).
- **What Snowstorm does instead, deliberately:** `Import` adds a `handle → path` row to a JSON registry,
  where the path is a mounted name (`/Game/...`) rather than wherever the file sat on the authoring
  machine. There IS a cook step now: meshes, textures, shaders and IBL cook to artifacts under
  `Engine/cache`, keyed on a **content hash** (mtime is only a cheap gate), driven by `--cook.assets` or
  *File > Cook Assets*. What is still missing is the `.meta` sidecar, hot-reload, async streaming, and a
  GUID separate from the path. The honest upgrade path from here: `.meta` sidecars (which is also what
  would let a texture's compression role be overridden per asset rather than derived from material
  slots) → a file watcher → async streaming. Treat `AssetRegistry` / `AssetManagerSingleton` as the seam
  where that grows.

  **`AssetManagerSingleton::ImportFile` is the one way a file enters the project** (*File > Import Asset*).
  It dispatches on `AssetTypeFromPath`; non-mesh types go through `PlanImport` into `<assets>/<type>/`
  (`DefaultImportFolder`), never overwriting (a different file at the same name takes `_N`, an identical
  one is reused), and meshes go through `ImportModelAssets` (copy, `.ssmat` generation, registry rows; no entities).
  `InstantiateImportedModel` spawns the entities and runs only when `ImportFile(path, true)` asks (*Import Asset* and
  the `--scene.bake` path through `ImportModel`); a Content Browser drop imports assets only, like Unity, Unreal and Godot.
  It does not save the registry.

  **An asset's identity is `AssetRegistry`'s `Canonicalize`, and it has exactly one spelling.** A path
  arrives mounted, absolute, or in the legacy project-relative form, and all three must key to one
  handle. When they did not, every content-browser scan re-imported every asset under a fresh handle and
  the registry grew by its own size per run, with no error and no crash: it reads as an ordinary content
  change and gets committed by a routine `git add`. `AssetIdentityTests` pins it.

  **A save of an unchanged registry is byte-identical.** `AssetRegistry::Serialize` writes rows sorted by
  path, since the handle map is unordered and every scene save rewrites the file. The Content Browser
  auto-imports every file under a project's `assets/`, so editor-owned content lives under `Engine/`
  (the light-icon atlas is `Engine/Icons/icons.png`), never in a project. `AssetRegistrySerializeTests`
  pins both, and fails on a committed registry that is not in sorted form.

**Cache eviction.** `MeshLibrary`, `ShaderLibrary` and `AssetManagerSingleton`'s handle caches are swept
by `AssetManagerSingleton::EvictUnreferenced()` on Open Scene, after the old entities are cleared: an entry
goes when its `shared_ptr` has no owner besides the caches (components, pipelines and in-flight compiles
count as owners). Evicted objects wait `GetFramesInFlight()+1` frames in a `RetireQueue` before their GPU
buffers are released. Textures are not swept yet.

## Verify before claiming

- This is graphics code: "renders/looks correct" can only be confirmed by **building and running**
  on a machine with a GPU/display. Headless verification is not possible, so say so when you can't run it.
- After non-trivial runtime changes, **build then run `Scripts/smoke-test.py`** (see Smoke test
  in `Scripts/README.md`): it catches crashes, hangs, and Vulkan validation/assertion errors that compilation can't.
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
  didn't update the timestamp, the build failed silently, so fix that first. This is the #1 cause of
  "my change isn't taking effect."
- **A running editor locks the exe.** `LNK1168: cannot open ... for writing` means a previous instance
  is still alive; `taskkill //IM Snowstorm-Editor.exe //F` before rebuilding. A leftover process also
  means you may be looking at an old build.
- Strip all temporary debug probes (logs, on-screen text) before committing, and `git diff` each
  touched file to catch leftovers, since incremental edits during debugging are easy to forget.

### Don't turn the user into your debugger

- Prefer verification you control: headless runs (`SS_SMOKE_FRAMES=N`), startup-time logging, and
  reading source/state. Reserve "please click X and tell me what you see" for genuine final visual
  confirmation, not for diagnosing logic: a manual launch/click/report loop burns the user's time
  and stalls on build/timing artifacts.
- **Keep effort proportional.** Time-box cosmetic/nice-to-have features; if one can't be made to work
  and verified in a couple of clean attempts, drop it rather than rabbit-holing. Commit the larger
  body of working, verified changes promptly instead of leaving it uncommitted while chasing a detail.

### How to debug effectively (don't guess in a loop)

- **Use the instrumentation that already exists BEFORE writing ad-hoc probes.** This engine already has
  rich, always-on timing/state readouts. Check them first instead of scattering `SS_CORE_WARN` probes:
  - The editor's **Performance panel** (`Snowstorm-Editor/Source/System/SceneHierarchySystem.cpp`) shows
    per-phase + per-**system** CPU ms, per-**pass GPU** ms (timestamp scopes), draw/batch/instance/
    triangle counts, and cull stats, smoothed and heat-colored. A "which part of the frame is slow"
    question is usually answered by reading this, not by instrumenting.
  - `SystemManager::GetSystemTimingsMs()` / `GetPhaseTimingsMs()`: per-system/phase CPU time (the same
    data the panel draws), queryable in code.
  - `CommandContext::BeginGpuScope`/`CollectGpuScopes` (`RendererService::GetGpuPassTimes()`): per-pass
    GPU timestamps.
  - The **frame-time watchdog** (`debug.max_frame_ms` CVar / `--max-frame-ms` smoke flag) turns a
    per-frame stall into a headless `[error]` naming the exact frame + duration.
  - The **profiler** (`SS_PROFILE_SCOPE` / `SS_PROFILE_FUNCTION`) for a full cross-thread timeline. Two
    back-ends behind the same macros: **Tracy** (primary, live; connect the Tracy GUI to a running Debug
    build over the network; `TRACY_ENABLE` is on in Debug) and a **headless JSON fallback**
    (`profile.capture_frames` / `profile.capture_path` CVars dump a chrome://tracing / Perfetto file with
    no GUI, for automated/offline trace analysis). Instrumented spots: frame-loop phases, every ECS system,
    and JobSystem worker tasks. One `SS_PROFILE_SCOPE` per lexical scope (it declares a fixed-name RAII
    object, so two in the same block is a redefinition; nest them).
  Per-pass GPU scopes also reach Tracy as GPU zones (one context per queue, `VulkanTracyGpuZones`), so
  GPU passes sit under the CPU timeline once a Tracy GUI is connected.
  A whole debugging session was once burned scattering probes to find a load spike that the Performance
  panel would have pinned to `RenderSystem`/shadow-fit in one glance (and the fix, reading the panel, was
  already built). If the existing readouts genuinely don't cover the spot, ADD a permanent, toggleable
  diagnostic there (extend the panel / add a scope / a CVar-gated log) rather than a throwaway probe;
  see "Build the engine to be debuggable" below. Reserve ad-hoc probes for gaps the standing tools can't
  reach, and strip them before committing.
- **Bisect, don't guess.** When behavior contradicts the code, the bug is somewhere between "what I
  believe is true" and "what's observed." Add a probe that splits that gap in half and *prove* which
  side is wrong, rather than changing code speculatively and re-running. One well-placed probe beats
  five hopeful edits.
- **One assumption per probe; isolate the variable.** Each test should answer exactly one yes/no
  question. If a result is paradoxical (e.g. "metadata valid at registration but absent at render"),
  do not theorize further: put *both* readings in a *single build/run* and compare. Contradictions
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

### Debugging rendering bugs specifically (lead with observation, not code)

A plausible cause is not a proven cause. On a flickering-texture bug the obvious-looking culprits
(missing mipmaps, near-plane z-fighting, depth precision) were all *wrong*: each was "fixed" before
being proven, wasting three rounds. What actually found it: the user's observations + visual probes.

- **Ask "when does it NOT happen?" before reading code.** Which scene only? Which material only?
  Static camera or only in motion? Each answer eliminates a whole class of causes in one sentence.
- **Static vs motion is the big discriminator.** Garbage/flicker *while the camera is static* ⇒ data
  changing per-frame: a race, sync gap, or undefined behaviour (e.g. non-uniform descriptor indexing).
  Shimmer *only under motion* ⇒ aliasing / mip-LOD / depth precision. Pin this first; it splits the
  search space in half immediately.
- **Bisect with temporary shader probes, not theory.** Force a flat color (geometry vs sampling),
  force texture index 0 (slot vs index), output a value as RGB (index magnitude, `SV_InstanceID`),
  force a mip level (`SampleLevel`). Each probe is one yes/no that halves the space; 4 to 5 pin it. This
  is "bisect, don't guess" applied to the GPU. Strip every probe before committing.
- **Don't "fix" before the probe proves the cause.** Prove with one probe, *then* change code.
- **Bindless + instancing red flag:** when one draw renders many objects with *different*
  descriptor-array indices, the index is not dynamically uniform → wrap in `NonUniformResourceIndex()`
  and enable the matching `shader*ArrayNonUniformIndexing` device feature. Silent garbage/flicker
  otherwise; it "works" pre-instancing only because each object was its own draw.
- **A wrong fix that's independently useful can stay.** Misdiagnoses (mipmaps, near plane) were real
  improvements on their own, so keep them; don't revert good changes just because they missed this bug.
- **Color/hue/gamma bug → suspect the color space and pipeline STAGE before the formula.** On a color
  bug, if two fixes fail in the *same category*, stop tweaking the math and ask: wrong color space
  (linear vs display/sRGB, HDR vs tonemap-compressed, RGB vs signed-chroma like YCoCg) or wrong pipeline
  stage (before vs after tonemap)? Several correct-looking diagnoses that all trace to one structural
  fact = that fact is the bug. Worked example (#44): an in-resolve TAA sharpen corrupted edge colors
  through five formula rewrites (signed-chroma clamp, then dark-biased low-pass, then pre-tonemap hue
  shift), all one root cause: **sharpening in linear HDR before the per-channel ACES tonemap**, which curves
  R/G/B differently and turns any overshoot into a hue shift. This is the "change altitude after ~2
  failed attempts" rule applied to color: interrogate placement, not parameters.

**Pipeline-stage invariant (learned from #44).** The **temporal resolve runs in linear HDR and does
accumulation only**. Perceptual / display-space operations (**sharpening, CAS, contrast, any
per-channel curve**) belong **after** tonemap (a post-tonemap pass, like FXAA on the LDR present
target), NEVER inside the resolve or any pre-tonemap linear-HDR stage: an overshoot that's a neutral
brightness change in linear becomes a **hue shift** once ACES curves each channel. One pass = one
responsibility: don't bolt a display-space effect onto a linear-HDR pass.

### Build the engine to be debuggable

The deeper fix for "I couldn't verify without the user" is to make state inspectable in code:

- **Expose state to headless inspection.** If you can only confirm a feature by looking at the screen,
  add a non-visual path to read the same truth: a startup/CVar-gated dump, a query function, or a log.
  The inspector's reflection (RTTR) and the `smoke.frames` hook already make a lot of state reachable
  without a GPU, so prefer wiring new state through those.
- **Prefer pure, testable cores.** Logic that maps data→data (name formatting, layout math, value
  conversions, asset-handle resolution) should live in free functions that a Catch2 test or a headless
  run can exercise directly, not be entangled in an ImGui draw call that only runs on a click.
- **Fail loud, not silent.** Silent fallbacks (a missing asset resolving to null, an unread metadata
  key, a default value) hide bugs and force interactive spelunking. Log once at `[error]`/`[warn]`
  when an expectation is violated, the way `ResolveAssetName` / the unresolved-handle path do.
- **Name things for diagnosis.** Vulkan objects via `SetVulkanObjectName`, ImGui widgets with stable
  unique IDs, components with reflected type names, so logs, validation, and RenderDoc say
  `Swapchain[0]` / `DIRECTIONAL LIGHT`, not an opaque handle.
- **Treat "I had to add a temporary on-screen probe" as a missing feature.** It usually means that
  state should be permanently visible (a debug overlay / stats panel / CVar dump). Consider promoting
  the probe into a real, toggleable diagnostic instead of deleting it.
