# ECS access declarations and scheduling (experimental)

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.*

**Experimental.** `ecs.schedule` defaults to false and, when on, only reorders systems: stages run serially, nothing runs in parallel between systems (#85). No timing is committed for any part of this page; every performance question below is "not measured" and names the command that answers it.

## Declaring access

`System::DeclareAccess(SystemAccess&)` states what a system reads and writes. Every core system overrides it, and `SystemAccessTests` fails if one does not. A system that does not override it is undeclared, which means exclusive and main-thread-only.

| Declaration | Meaning |
| --- | --- |
| `ReadComponents<T...>()`, `WriteComponents<T...>()` | Component types the system reads or mutates |
| `ReadResources<T...>()`, `WriteResources<T...>()` | Singletons and Services it touches |
| `Structural()` | It emplaces, removes or destroys, so it needs a stage to itself |
| `TrackingMapResource` (a resource tag) | The change-tracking map: written by tracked mutators, read by `Init/Changed/FiniView` |

C++ cannot introspect a system body, so the declaration is the contract: a missing entry is a silent scheduling bug, not a compile error. Declaring components alone would hide conflicts between systems that both mutate a shared Service, which is why resources are part of it. `RotatorSystem`, for example, reads `RotatorComponent` and `CameraPathComponent`, writes `TransformComponent`, reads `RendererService` and `EditorHooksSingleton`, and writes `TrackingMapResource`.

Two declarations conflict (`SystemAccess::ConflictsWith`) when one writes a type the other reads or writes. An undeclared or structural system conflicts with everything.

The reference models are Unity DOTS (`RefRO`/`RefRW` queries that order jobs) and Bevy (systems ordered by declared access). Unity overlaps jobs rather than whole systems by default; this engine currently stops one step earlier, at deriving the order.

## Staging

`BuildSchedule` (`ECS/SystemSchedule.hpp`) layers one phase's systems, given in registration order, into stages:

- Stage k holds the systems whose conflicting predecessors are all in stages below k.
- A conflicting pair keeps its registration order (the earlier system lands in an earlier stage).
- Systems that do not conflict may share a stage; indices inside a stage ascend.
- Undeclared and structural systems land alone.

`ScheduleOrder` flattens the stages into an execution order over the systems active this frame. The edit-mode gate (`RunsInEditMode()`) is applied before staging, so a skipped system never holds up its dependents.

| CVar | Default | Effect |
| --- | --- | --- |
| `ecs.schedule` | off | Run each phase in `ScheduleOrder` instead of registration order, stage by stage, still serial |

Phases still run in enum order. With the CVar off, behaviour is registration order exactly as before. Output is unchanged when the declarations are right, because every conflicting pair keeps its order.

Tests (`SystemAccessTests`, CPU-only, run by `ctest`): disjoint systems share a stage; conflicts keep registration order; structural and undeclared systems run alone; a tracked read follows a tracked write through `TrackingMapResource`; the core systems schedule every system exactly once with ascending indices per stage; `ScheduleOrder` drops skipped systems without holding anything up.

**Not built:** dispatching a stage's systems as jobs. That needs `JobSystem` to be safe against workers that wait on nested `ParallelForEach` chunks, and the design says to decide it on per-system CPU numbers (`GetSystemTimingsMs`, the Performance panel). How many stages in the real core set hold two or more systems of measurable cost is not measured.

## Parallelism inside a system

The larger wins are within a system, and both primitives are opt-in and keep output bit-identical to serial.

- **`System::ParallelForEach<Read<T>/Write<T>...>`** runs a body per entity across `JobSystem` workers. Tags decide constness (`Read<T>` gives `const T&`, `Write<T>` gives `T&`) and which components get marked changed. The body may mutate only its own entity's `Write<>` components: no shared state, no other entities, no `TrackedRegistry` mutation APIs. One bulk change marker is recorded after the barrier, O(1) in entity count, so `ChangedView` still works. `RotatorSystem` is the reference.
- **`JobSystem::ParallelGather<T>(count, body, emit)`** is the filter-and-collect form: each chunk fills its own bucket and the buckets concatenate in index order, so the result is deterministic whatever the thread schedule. `VisibilitySystem`'s frustum cull is the reference.
- Both take a grain size (256 by default in the callers above) and degrade to an inline serial pass when the count fits in one grain or there is one worker.

Most systems do not qualify (they submit to the renderer, touch singletons or run scripts) and stay serial. That is a decision, not a missed optimisation.

| CVar | Default | Effect |
| --- | --- | --- |
| `ecs.parallel` | on | Off forces the serial path (a grain at least the count), the A/B switch |

## The benchmark

`--ecs.benchmark` runs headless at startup: a throwaway `World` of bare `Transform` and `Rotator` entities, `RotatorSystem::Execute` timed serial (`ecs.parallel` off) then parallel across a sweep of entity counts, median per-frame ms over 200 frames after 20 warmup frames, a table in the log, then exit. It times the whole `Execute` (snapshot, dispatch and the O(n) change-mark), with no renderer or GPU in the loop. The change-mark caps the end-to-end speedup at scale even when the compute scales near-linearly.

```
Snowstorm-Editor --ecs.benchmark
```

**Not measured.** No table from this benchmark is committed, so this page quotes no CPU, core count, commit or speedup. Record a run here with its CPU, thread count, commit and date when one exists.

## Limits

- Stages only reorder: no overlap between systems.
- A wrong declaration is not detected at runtime.

Related issues: #85, #321.
