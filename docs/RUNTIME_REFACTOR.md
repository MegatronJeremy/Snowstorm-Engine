# Sketch: a Runtime/Player target + editor-independent system registration

Goal: make the engine runnable **without the editor**, so that (a) what ships to players is what
runs in the editor's play mode, and (b) the editor/runtime dependency direction is enforced. This
addresses review concern #2 ("no runtime path independent of the editor") and gives `Snowstorm-App`'s
slot a real job.

> Status: design sketch. None of the snippets below are compiled — they show shape and intent, not
> final code. File/line references are against the current tree.

## The problem, precisely

Today **all** systems are registered in `EditorLayer::RegisterSystems()`
(`Snowstorm-Editor/Source/EditorLayer.cpp:155`), and engine systems are **interleaved** with editor
systems:

```
RuntimeInitSystem, ScriptSystem, CameraControllerSystem, ShaderReloadSystem,   // engine
DockspaceSetupSystem, ViewportResizeSystem, ViewportDisplaySystem,             // editor (UI)
EditorMenuSystem, EditorNotificationSystem, SceneHierarchySystem,              // editor (UI)
CameraRuntimeUpdateSystem,                                                     // engine
MeshResolveSystem, MaterialResolveSystem,                                      // engine
MandelbrotControllerSystem,                                                    // editor example
LightingSystem, VisibilitySystem, RenderSystem                                 // engine
```

Because the editor systems sit *in the middle*, you can't just split into
"RegisterCoreSystems(); RegisterEditorSystems();" — that would push the UI systems after Render.
The fix is to register systems into **named phases** with a fixed execution order, so each side
contributes systems to the phases it owns and the order is defined by the phase, not by call order.

## Target layout

```
Snowstorm-Core      runtime library (engine) — owns the core systems + phase order
Snowstorm-Editor    executable, links Core, adds UI-phase systems
Snowstorm-Runtime   NEW executable, links Core, no editor — the "player"
(Snowstorm-App)     delete (legacy OpenGL sandbox)
```

---

## Step 1 — phase-aware `SystemManager`

Replace the flat `std::vector<Scope<System>>` (`ECS/SystemManager.hpp:39`) with one bucket per phase.
Execution iterates phases in enum order, then systems within a phase in registration order.

```cpp
// ECS/SystemPhase.hpp
namespace Snowstorm
{
    enum class SystemPhase : uint8_t
    {
        Init,       // one-time/lifecycle resolve (RuntimeInitSystem)
        Logic,      // scripts, input-driven controllers
        AssetSync,  // hot-reload, shader/asset watchers
        UI,         // editor/ImGui systems — EMPTY in a packaged runtime
        Resolve,    // handles -> runtime resources (mesh/material/camera runtime)
        PreRender,  // lighting, visibility/culling, example controllers
        Render,     // submit (RenderSystem — always last)
        _Count
    };
}
```

```cpp
// ECS/SystemManager.hpp (shape)
template <typename T, typename... Args>
void RegisterSystem(SystemPhase phase, Args&&... args)
{
    static_assert(std::is_base_of_v<System, T>);
    m_Phases[static_cast<size_t>(phase)].emplace_back(CreateScope<T>(m_World, std::forward<Args>(args)...));
}

void ExecuteSystems(Timestep ts)
{
    for (auto& bucket : m_Phases)          // phases in enum order
        for (auto& system : bucket)        // registration order within a phase
            system->Execute(ts);
    m_Registry.ClearTrackedComponents();
}

private:
    std::array<std::vector<Scope<System>>, static_cast<size_t>(SystemPhase::_Count)> m_Phases;
```

This also kills the fragile `// should be BEFORE` comments — order is now explicit and self-documenting.

## Step 2 — Core owns the engine systems

Move the engine-system registration out of the editor into Core, keyed by phase. All these systems
already live in Core (`Systems/`, `Lighting/`), so this is just relocating the calls.

```cpp
// Snowstorm/Systems/CoreSystems.hpp
namespace Snowstorm { void RegisterCoreSystems(World& world); }

// Snowstorm/Systems/CoreSystems.cpp
void Snowstorm::RegisterCoreSystems(World& world)
{
    auto& sm = world.GetSystemManager();
    sm.RegisterSystem<RuntimeInitSystem>      (SystemPhase::Init);
    sm.RegisterSystem<ScriptSystem>           (SystemPhase::Logic);
    sm.RegisterSystem<CameraControllerSystem> (SystemPhase::Logic);
    sm.RegisterSystem<ShaderReloadSystem>     (SystemPhase::AssetSync);
    sm.RegisterSystem<CameraRuntimeUpdateSystem>(SystemPhase::Resolve);
    sm.RegisterSystem<MeshResolveSystem>      (SystemPhase::Resolve);
    sm.RegisterSystem<MaterialResolveSystem>  (SystemPhase::Resolve);
    sm.RegisterSystem<LightingSystem>         (SystemPhase::PreRender);
    sm.RegisterSystem<VisibilitySystem>       (SystemPhase::PreRender);
    sm.RegisterSystem<RenderSystem>           (SystemPhase::Render);
}
```

(Optionally fold this into `World` so a bare `World` is renderable by default — but a free function
keeps `World` from depending on every system header.)

## Step 3 — the editor adds only its own systems

`EditorLayer::RegisterSystems()` shrinks to the editor + example systems, all in the `UI`/`PreRender`
phases. It no longer knows about RenderSystem, lighting, resolve, etc.

```cpp
void EditorLayer::RegisterSystems() const
{
    auto& sm  = m_ActiveWorld->GetSystemManager();
    auto& sim = m_ActiveWorld->GetSingletonManager();
    sim.RegisterSingleton<EditorNotificationsSingleton>();

    sm.RegisterSystem<DockspaceSetupSystem>     (SystemPhase::UI);
    sm.RegisterSystem<ViewportResizeSystem>     (SystemPhase::UI);
    sm.RegisterSystem<ViewportDisplaySystem>    (SystemPhase::UI);
    sm.RegisterSystem<EditorMenuSystem>         (SystemPhase::UI);
    sm.RegisterSystem<EditorNotificationSystem> (SystemPhase::UI);
    sm.RegisterSystem<SceneHierarchySystem>     (SystemPhase::UI);
    sm.RegisterSystem<MandelbrotControllerSystem>(SystemPhase::PreRender); // example
}
```

And `EditorLayer::OnAttach()` calls the core registration first:

```cpp
m_ActiveWorld = CreateRef<World>();
RegisterCoreSystems(*m_ActiveWorld);   // engine systems
RegisterSystems();                     // editor systems on top
LoadOrCreateStartupWorld();
```

The `UI` phase is empty in a packaged runtime, so the exact same engine phases run in both, in the
same order — that's the property that makes "play mode == shipped build".

## Step 4 — the `Snowstorm-Runtime` target (the player)

> **Status (scaffolded):** the target exists (`Snowstorm-Runtime/`) and reuses `RegisterCoreSystems`,
> but it does **not** present to screen yet. Building the scaffold surfaced a gap the original sketch
> missed: **the swapchain is composed entirely by the editor's ImGui pass.** In `RenderSystem.cpp`
> the scene renders into offscreen `RenderTarget`s, and the *only* pass that targets the swapchain is
> `"EditorPass"`, whose body is `Renderer::RenderImGuiDrawData(c)` — i.e. the window you see is ImGui
> drawing the viewport texture. A runtime with no ImGui therefore renders the scene to an offscreen
> target and presents nothing → blank window.
>
> To avoid a crash, `RenderSystem` now guards that pass with `Renderer::IsImGuiBackendInitialized()`
> (true only when the editor brings the ImGui backend up). The remaining work — a **present path**
> that blits the primary camera's render target to the swapchain (or renders the primary camera
> directly into it) — is Vulkan code that must be written and **verified on a GPU**, so it's
> deliberately left for a session where the engine can be built and run.

Mirror `SnowstormEditor.cpp` (`Snowstorm-Editor/Source/SnowstormEditor.cpp`), minus `ImGuiService`
and the editor layer.

```cpp
// Snowstorm-Runtime/Source/RuntimeLayer.cpp
void RuntimeLayer::OnAttach()
{
    m_World = CreateRef<World>();
    RegisterCoreSystems(*m_World);                 // SAME engine systems as the editor
    if (!SceneSerializer::Deserialize(*m_World, "assets/scenes/Startup.world"))
        SS_CORE_ERROR("Runtime: no startup scene to load.");
}
void RuntimeLayer::OnUpdate(Timestep ts) { m_World->OnUpdate(ts); }
```

```cpp
// Snowstorm-Runtime/Source/RuntimeMain.cpp
namespace Snowstorm
{
    class SnowstormRuntime final : public Application
    {
    public:
        SnowstormRuntime() : Application("Snowstorm-Runtime") { PushLayer(new RuntimeLayer()); }
    };
    Application* CreateApplication() { return new SnowstormRuntime(); }   // no ImGuiService
}
```

```cmake
# Snowstorm-Runtime/CMakeLists.txt  (copy of the App/Editor pattern)
add_executable(Snowstorm-Runtime)
set_target_properties(Snowstorm-Runtime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
file(GLOB_RECURSE RT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
target_sources(Snowstorm-Runtime PRIVATE ${RT_SOURCES})
target_include_directories(Snowstorm-Runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/Snowstorm-Core/Source)
target_link_libraries(Snowstorm-Runtime PRIVATE Snowstorm-Core)
```

Root `CMakeLists.txt`: drop `add_subdirectory(Snowstorm-App)`, add `add_subdirectory(Snowstorm-Runtime)`.

This is also a natural home for a future **headless mode** (a `--headless` flag that skips swapchain
present) — the cheapest way to smoke-test the engine in CI without a GPU display.

## Step 5 — CI so a target can never silently rot again

The reason `Snowstorm-App` died unnoticed is that nothing built the full solution. A single Windows
job closes that gap:

```yaml
# .github/workflows/build.yml (sketch)
name: build
on: [push, pull_request]
jobs:
  windows:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: '3.x' }
      - name: Configure (vcpkg + CMake)
        run: py Scripts/Generate-Solution.py
      - name: Build all targets
        run: cmake --build build --config Debug
```

(vcpkg-from-source is slow; cache `vcpkg/installed` with `actions/cache` keyed on the package list.)

---

## Suggested order of execution

1. Delete `Snowstorm-App` (separate, trivial, gets the build green). 
2. Add `SystemPhase` + phase buckets to `SystemManager` (behavior-preserving if buckets are filled in
   the current order).
3. Add `RegisterCoreSystems`; switch `EditorLayer` to call it + register only editor systems. Verify
   the editor looks identical.
4. Add `Snowstorm-Runtime`; confirm it opens a window and renders the startup scene with no ImGui.
5. Add CI.

## Risks / things to watch

- **Ordering regressions.** The phase bucketing above reproduces today's order, but anything relying
  on a specific interleave (e.g. a UI system running before `CameraRuntimeUpdateSystem`) must keep
  that relationship — double-check the `UI` vs `Resolve` boundary against current behavior.
- **Singletons.** `EditorNotificationsSingleton` is editor-only; keep it registered editor-side. The
  runtime must not assume editor singletons exist.
- **Startup scene.** The runtime needs a serialized scene to load; today the startup scene is created
  *by the editor* on first run (`EditorLayer::LoadOrCreateStartupWorld`). The runtime should fail
  loudly (not crash) if it's missing.
- **`World`-scoped GPU singletons** (review concern #6) are orthogonal but will matter once a runtime
  loads scenes repeatedly.
