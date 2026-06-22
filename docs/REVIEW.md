# Snowstorm Engine — Architecture & Code Review

A snapshot review of the engine as of this commit. Grouped into **strengths**, **architectural
concerns** (big-picture design), **concrete issues** (localized cleanups), **process gaps**, and a
**TODO/unfinished inventory** pulled from the code. File references are `path:line`.

> Not build-verified: the review is from reading the source, not from a clean build or runtime.
> Items marked _(high confidence)_ are near-certain from the code; treat the rest as recommendations.

---

## Strengths

- **Clean layered split** — `Core` (static lib) / `App` / `Editor`, with a backend-agnostic
  `RendererAPI` and the Vulkan implementation isolated under `Platform/Vulkan`. The render layer is
  built on a modern stack (volk, Vulkan Memory Allocator, SPIRV-Reflect, gli).
- **Data-driven ECS** on EnTT with a thoughtful reactive layer: `TrackedRegistry` exposes
  Added/Removed/Changed views so systems can react to lifecycle events, not just poll.
- **Asset + scene pipeline** — asset handles, a JSON asset registry, scene (de)serialization, and
  **RTTR** component reflection, which gives the editor and the serializer component metadata for free.
- **Capable editor** — ImGui dockspace, scene hierarchy, viewport render targets, per-entity
  material overrides, and Scene/Game visibility masks. That's a lot of surface for a hobby engine.
- **Solid hygiene** — `Ref`/`Scope` aliases, `SS_*` assert/log macros, a Chrome-tracing profiler,
  `NonCopyable`, consistent naming, and correct `.gitignore` / `.gitattributes`.

---

## Architectural concerns (worth addressing as the engine grows)

### 1. `Snowstorm-App` is stale and almost certainly breaks a clean build _(high confidence)_
`Snowstorm-App/Source/SandboxApp.cpp` (and `Sandbox2D`) use the **pre-Vulkan OpenGL-era API** —
`VertexArray`, `OrthographicCameraController`, `RenderCommand`, `Platform/OpenGL/OpenGLShader.h`,
`ShaderDataType`/`BufferLayout`. None of those symbols exist anywhere in the current tree, and the
umbrella `Snowstorm.h` no longer includes them. Yet the target is still globbed
(`Snowstorm-App/CMakeLists.txt`) and added in the root `CMakeLists.txt:13`. A from-scratch build of
the full solution should fail on the App target. **Decide: delete it, or port it to the current
renderer.** Right now it's dead weight that hides whether the project builds end-to-end.

### 2. No runtime/game path that is independent of the editor
Every *engine* system (`RenderSystem`, `LightingSystem`, `VisibilitySystem`, `MeshResolveSystem`,
`MaterialResolveSystem`, camera systems, …) is registered inside
`EditorLayer::RegisterSystems()` (`Snowstorm-Editor/Source/EditorLayer.cpp:155`), mixed in with
editor-only systems. The author already flags this (`// TODO do this better somehow and not in
EditorLayer`). To ship a standalone game you'd have to reproduce this list. Suggest the engine own a
default system registration (e.g. a `World::RegisterCoreSystems()` or a `SceneRenderer`), and let the
editor *add* only editor systems on top.

### 3. System execution order is implicit and hand-maintained
Order = registration order, governed by comments like `// should be BEFORE display system` and
`// RenderSystem should always be last` (`EditorLayer.cpp:167-185`). There's no declared dependency
or phase model, so a reordering bug is silent. As the system count grows, consider explicit phases
(PreUpdate / Update / PostUpdate / Render) or declared before/after dependencies.

### 4. `TrackedRegistry` reinvents EnTT facilities at a cost
The reactive layer (`Snowstorm-Core/Source/Snowstorm/ECS/TrackedRegistry.hpp`) stores per-frame
`unordered_map<entity, unordered_set<type_index>>` for added/removed/changed, and
`AddedView/ChangedView/RemovedView` **allocate a new `unordered_set` and scan every tracked entity**
on each call (`TrackedRegistry.hpp:230-278`). EnTT already provides `on_construct`/`on_update`/
`on_destroy` signals, `entt::observer`, and reactive storage that do this natively and far more
cheaply. There's also a documented footgun: "Changed" is only tracked if you mutate via the wrapper
(`get<T>()` escape hatch bypasses it, `TrackedRegistry.hpp:11-20`). Migrating to EnTT observers would
cut allocations and remove the footgun.

### 5. `ServiceManager` and `SingletonManager` are duplicated, and the Service/Singleton split is implicit
The two managers are near-identical `type_index → unique_ptr` maps with copy-pasted Register/Get
(`Service/ServiceManager.hpp`, `ECS/SingletonManager.hpp`), both using `new T(...)` + `.reset()`
instead of `CreateScope`. They could share one templated `TypeMap`. More importantly, the semantic
difference — **Service = application scope, Singleton = world scope** — is nowhere documented; it's
inferable only from where each is constructed. Also note `ServiceManager` iterates an
`unordered_map`, so service `OnUpdate`/`PostUpdate` order is **non-deterministic**.

### 6. World-scoped singletons may be wrong for GPU/asset state
`ShaderLibrarySingleton`, `MeshLibrarySingleton`, `AssetManagerSingleton`, and `RendererSingleton`
are registered per-`World` (`World.cpp:24-31`). Creating/loading a second world would recreate them
and reload GPU assets. Asset/mesh/shader caches almost certainly belong at app/engine scope, shared
across worlds.

### 7. `Renderer` is fully static/global
All of `Renderer` is static (`Render/Renderer.hpp`), with a global `s_API` and a static
`s_FrameUniformRings` (with `// TODO move this to RendererAPI`). Fine today, but it blocks multiple
devices/windows and makes the renderer hard to test or instance.

---

## Concrete issues / cleanups (localized)

- **Dead code:** `Application::OnWindowClose` / `OnWindowResize` are defined and declared but never
  called — the real handling is inline in `OnEvent`'s switch (`Core/Application.cpp:84-158`). Remove
  them or route events through them. _(high confidence)_
- **Duplicate macro:** `BIND_EVENT_FN` is re-defined locally in `Application.cpp:13`, duplicating
  `SS_BIND_EVENT_FN` from `Base.hpp:63`.
- **Profiler is always on:** `#define SS_PROFILE 1` is hardcoded (`Debug/Instrumentor.hpp:130`), so
  instrumentation *and* JSON trace-file writing happen in **release** builds too. Gate it on
  `SS_DEBUG` or a CMake option.
- **Error handling disappears in release:** 232 `SS_ASSERT`/`SS_CORE_ASSERT` and 0 exceptions, and
  asserts compile out unless `SS_DEBUG` (`Base.hpp`). In release, conditions like "Service not
  registered" or an unimplemented backend silently proceed with garbage. Decide which checks must be
  always-on (add an `SS_VERIFY` that survives release) vs debug-only.
- **`vkDeviceWaitIdle` scattered (13 call sites)**, including inside buffer/sampler creation
  (`Platform/Vulkan/VulkanBuffer.cpp:90`, `VulkanSampler.cpp:44`) — full GPU stalls, self-flagged as
  "probably really bad practice". Replace with a transfer/staging queue and a deferred deletion queue.
- **VSync hardcoded:** present mode pinned to `VK_PRESENT_MODE_FIFO_KHR` (`VulkanContext.cpp:315`),
  and `WindowsWindow::SetVSync` is a no-op (`WindowsWindow.cpp:47`).
- **Demo content leaks into Core:** `MaterialType::Mandelbrot` lives in the core asset enum
  (`Assets/MaterialAsset.hpp:13`, marked `// TODO THIS SHOULD NOT BE HERE`), and the
  `MandelbrotControllerSystem` (an Editor example) is part of the core update order. Keep example
  material types/systems out of Core.
- **Hardcoded frames-in-flight:** `MaterialInstance` hardcodes `s_NumFrames = 2` and a dirty counter
  of `2` (`Render/MaterialInstance.cpp:16`, `MaterialInstance.hpp:39`) instead of asking
  `Renderer::GetFramesInFlight()`.
- **Pipeline defaults:** depth test is forced on for all materials
  (`Assets/AssetManagerSingleton.cpp:166`, `// TODO these shouldn't be enabled always...`).
- **Manual RTTR registration:** components are registered one by one with
  `// TODO move all of these to functions (and automate it somehow)`
  (`Components/ComponentRegistration.cpp:28`).
- **Material override mask:** flagged by the author as a disliked approach
  (`Components/MaterialOverridesComponent.hpp:28`).
- **Editor-only runtime component serialized in all builds:** `ViewportInteractionComponent` is
  marked "SHOULD ONLY BE SERIALIZED IN DEBUG BUILDS" (`Components/ViewportInteractionComponent.hpp:5`).

---

## Process gaps

- **No tests, no CI.** A minimal CI (a Windows GitHub Actions job that runs `Generate-Solution.py`
  and builds) would immediately catch the broken `Snowstorm-App`. A few headless unit tests —
  scene serialize→deserialize round-trip, asset registry load/save, `TrackedRegistry` add/change/
  remove semantics — would protect the data layer without needing a GPU.
- **Docs** now cover build/run/architecture at a high level (`README.md`, `CLAUDE.md`). A dedicated
  `ARCHITECTURE.md` (frame flow: Service update → layers → World systems → render; the ECS reactive
  model; the render-target/visibility model) would help onboarding — this document is a start.

---

## TODO / unfinished inventory (from the code)

| Area | File:line | Note |
| --- | --- | --- |
| Build | `Snowstorm-App/*`, root `CMakeLists.txt:13` | App uses removed OpenGL API; likely won't build |
| Render abstraction | `Render/{Pipeline,Shader,Texture,Sampler,DescriptorSet,DescriptorSetLayout}.cpp` | 9× `DX12 ... not implemented yet` asserts |
| Renderer | `Render/Renderer.hpp:47`, `Renderer.cpp:9,51` | move state to `RendererAPI`; DX12 stub |
| Renderer caps | `Render/RendererAPI.hpp:37` | `GetMinUniformBufferOffsetAlignment` → `GetCapabilities` |
| Material | `Render/MaterialInstance.cpp:16,106`, `.hpp:39,61` | hardcoded frame count; reflection bridge is temporary |
| Material asset | `Assets/MaterialAsset.hpp:13` | `Mandelbrot` enum doesn't belong in Core |
| Material IO | `Assets/MaterialAssetIO.cpp:13` | helpers should move to engine utilities |
| Asset mgr | `Assets/AssetManagerSingleton.cpp:166` | depth test always enabled |
| Components | `Components/ComponentRegistration.cpp:28` | automate RTTR registration |
| Components | `Components/MaterialOverridesComponent.hpp:28` | disliked override-mask approach |
| Components | `Components/ViewportInteractionComponent.hpp:5` | editor-only, shouldn't serialize in release |
| Vulkan | `Platform/Vulkan/VulkanBuffer.cpp:90`, `VulkanSampler.cpp:44`, `VulkanCommandContext.cpp:201` | `vkDeviceWaitIdle` on resource ops; graphics-only command context |
| Vulkan | `Platform/Vulkan/VulkanContext.cpp:315` | VSync hardcoded (FIFO) |
| Window | `Platform/Windows/WindowsWindow.cpp:47` | `SetVSync` is a no-op |
| Camera | `Systems/CameraControllerSystem.cpp:53` | controller logic "still kind of sucks" |
| Events | `Events/Event.hpp:57` | event-handling cleanup |
| App core | `Core/Application.cpp:29` | service-manager init note; dead window handlers |
| Core | `Core/Base.hpp:8` | `SS_DEBUG` define cleanup |
| Math | `Math/Math.hpp:6` | glm include hygiene |
| Editor | `EditorLayer.cpp:155,162` | system registration shouldn't live in the editor |
| Editor | `System/ViewportDisplaySystem.cpp:23`, `DockspaceSetupSystem.hpp:6` | editor-only component handling; ordering dependency |
| Renderer singleton | `Render/RendererSingleton.cpp:208` | "only one instance?" |
