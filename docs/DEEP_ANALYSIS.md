# Deep analysis — correctness, performance, scalability

A whole-project pass focused on the render hot path, descriptor/pipeline management, the ECS, and
the asset system. Complements [REVIEW.md](REVIEW.md) (architecture) and
[VULKAN_REVIEW.md](VULKAN_REVIEW.md) (frame loop / swapchain / device — not repeated here).

> Read-only review, **not profiled**. Performance/scalability claims are about algorithmic and
> structural cost (allocations, draw calls, GPU stalls, O(n) scans), not measured numbers. File refs
> are `file:line`. Cross-refs to existing issues are noted.

---

## Correctness

**C1 — `UniformRingBuffer` overflow is a silent out-of-bounds write in release.**
`Alloc` guards capacity with `SS_CORE_ASSERT` only (`UniformRingBuffer.hpp:82`); asserts compile out
in release, so `AllocAndWrite`'s `memcpy` (`:100`) then writes past the 4 MB mapped buffer →
heap/GPU memory corruption once per-frame uniform usage is exceeded (lots of objects). Needs a
release-safe path: grow, chain to a new buffer, or hard-fail. Ties to issue #12 (asserts vanish in
release).

**C2 — Index type is hardcoded to 32-bit.** `DrawIndexed` always binds
`VK_INDEX_TYPE_UINT32` (`VulkanCommandContext.cpp:318`, with a comment admitting it). Any mesh with
16-bit indices renders garbage or crashes. Store the index type on the buffer/mesh.

**C3 — Validation/asserts disappear in release** (cross-ref VULKAN_REVIEW #2 and issue #12): most of
the engine's safety net (232 asserts, the `VK_CHECK` macro paths, ring guard) is debug-only.

---

## Performance — per-frame render hot path

**P1 — No real GPU instancing, despite batching.** `DrawMesh` groups draws by `(mesh, material)`
(`RendererSingleton.cpp:61-83`), but `FlushBatch` then issues **one `vkCmdDrawIndexed` per instance
with `instanceCount = 1`** (`:197-214`, `// TODO only one instance?`). The batch only saves state
binds, not draw calls — the renderer is draw-call-bound. Fix: upload the batch's instance transforms
to an instance/storage buffer and issue a single `DrawIndexed(instanceCount = N)`.

**P2 — Frame UBO re-uploaded once per batch.** The `FrameCB` (view-proj, camera, lights) is identical
across all batches in a scene, but `SetData` runs inside every `FlushBatch`
(`RendererSingleton.cpp:154-164`, "optimize later"). Upload it once per `BeginScene`.

**P3 — Index buffer rebound per draw.** `DrawIndexed` calls `vkCmdBindIndexBuffer` every invocation
(`VulkanCommandContext.cpp:320`), so it's rebound for every instance of the same mesh. Bind once per
batch.

**P4 — `vkDeviceWaitIdle` on resource destruction** (cross-ref issue #11). Worst offender:
`VulkanDescriptorSet::DestroyVulkanObjects` full-stalls the GPU on **every** set destruction
(`VulkanDescriptorSet.cpp:99`). Churning materials/textures = repeated whole-device stalls. Needs a
deferred deletion queue keyed on frame fences.

**P5 — Overly broad pipeline barriers.** `TransitionLayout` uses
`srcStageMask = ALL_COMMANDS_BIT`, `srcAccessMask = MEMORY_WRITE_BIT` (`VulkanCommandContext.cpp:111-112`).
Correct but serializes the pipeline on every transition. Derive tight src masks from a layout→access
table.

**P6 — No `VkPipelineCache`.** `vkCreateGraphicsPipelines` is passed `VK_NULL_HANDLE`
(`VulkanGraphicsPipeline.cpp:450`), so there's no cross-pipeline or on-disk cache — slower pipeline
creation and longer load/first-use hitches. Create and persist a `VkPipelineCache`.

**P7 — No batch sorting.** Batches are flushed in insertion order (`RendererSingleton.cpp:97-101`);
interleaved pipelines cause redundant `vkCmdBindPipeline`. Sort by pipeline, then material.

**P8 — Synchronous asset loading on the main thread.** `GetMesh` / `GetTextureView` / `GetShader` /
`GetMaterialInstance` load and decode from disk on the calling (main) thread during the resolve
systems (`AssetManagerSingleton.cpp`). First touch of an asset (assimp import, image decode, shader
compile via dxc) stalls the frame. Move to async loading / streaming, or load-ahead at scene load.

**P9 — Every device-local upload is a full, blocking GPU round-trip.** `VulkanBuffer::SetDataInternal`
for non-host-visible buffers creates a staging buffer and calls `ImmediateSubmit`
(`VulkanBuffer.cpp:163-238`), which allocates a command buffer, creates a fence, submits to the
**graphics queue**, and `vkWaitForFences` to completion before returning (`VulkanCommon.cpp:46+`). So
loading one mesh = two blocking GPU syncs (vertex + index), serialized against rendering on the single
queue, plus a per-buffer `vkDeviceWaitIdle` on destruction (`VulkanBuffer.cpp:90`). It's correct
(fence-gated, so the staging free is safe) but it's the concrete mechanism behind P4/P8/S4 — a
dedicated transfer queue + batched uploads + a deletion queue would remove all of it.

---

## Scalability ceilings

**S1 — Brute-force culling, O(cameras × meshes).** `VisibilitySystem` loops every mesh for every
camera each dirty frame, with no spatial acceleration (`VisibilitySystem.cpp:70-129`). Fine to a few
thousand objects; won't scale to large worlds. Add a BVH/grid/octree and persistent per-entity
visibility. (The global dirty check also re-culls *everything* when *any* transform changes.)

**S2 — One `VkDescriptorPool` per descriptor set.** `CreatePoolAndAllocateSet` creates a dedicated
pool with `maxSets = 1` for **every** descriptor set (`VulkanDescriptorSet.cpp:70-89`; the code even
notes it). Every material instance and per-frame set spawns its own pool. At scale that's hundreds–
thousands of tiny pools. Use a growing free-list allocator: one (or few) pools, many sets, recycled.

**S3 — Per-entity material overrides explode descriptor/buffer counts.** An entity with overrides
gets a **unique** `MaterialInstance` (`MaterialResolveSystem.cpp:73-86`), and each instance allocates
per-frame UBOs + descriptor sets (`MaterialInstance::EnsurePerFrameResources`). N overridden entities
→ N × framesInFlight sets/buffers (each its own pool, per S2). Prefer per-instance data carried in the
instance buffer with a shared material.

**S4 — Single thread, single queue (the ceiling).** All systems, culling, command recording, and
asset loading run on the main thread, and the device is created with a single graphics+present queue
(`VulkanContext.cpp:178-234`). No job system, no secondary/parallel command recording, no dedicated
transfer queue. A transfer queue would also remove most `vkDeviceWaitIdle` uploads (P4). This is the
structural limit everything else hits eventually.

**S5 — Unbounded, world-scoped asset caches.** Caches never evict (`AssetManagerSingleton` has no
unload/LRU), so long sessions grow memory; and because they're world-scoped (issue #8), every scene
load reloads from scratch. Add ref-counted/LRU eviction at app scope.

**S6 — Reactive ECS views allocate and scan each call** (cross-ref issue #13). `AddedView` /
`ChangedView` build an `unordered_set` and scan all tracked entities per call; `VisibilitySystem`
alone calls several per frame.

---

## Done well (so the picture is balanced)

- **Per-frame-in-flight uniform ring with dynamic offsets**, reset under the frame fence — correct,
  cheap double-buffering (`UniformRingBuffer` + `Renderer.cpp:138-149`).
- **Handle-keyed caches** for meshes, shaders, textures, materials, and pipelines
  (`AssetManagerSingleton`) — repeated access is O(1).
- **Reactive dirty early-outs** in `VisibilitySystem` and the resolve systems — work only happens on
  change, not every frame.
- **Mesh bounds cached to disk** keyed by source write time (`AssetManagerSingleton.cpp:53-74`).
- **Cheap-reject culling**: sphere test before AABB (`VisibilitySystem.cpp:113-125`).
- **Modern Vulkan**: dynamic rendering (no render-pass objects), synchronization2, a bindless
  descriptor set (set 3), VMA, volk.

---

## Suggested issues (not yet filed)

| Priority | Area | Title | Refs |
| --- | --- | --- | --- |
| high | render/perf | Instanced draw submission (one DrawIndexed per batch) | `RendererSingleton.cpp:197-214` |
| high | render/scale | Pooled descriptor-set allocator (stop one-pool-per-set) | `VulkanDescriptorSet.cpp:70-99` |
| high | render/correctness | Ring overflow must be release-safe (no silent OOB) | `UniformRingBuffer.hpp:82,100` |
| medium | render/perf | Upload FrameCB once per scene, not per batch | `RendererSingleton.cpp:154-164` |
| medium | render/correctness | Store index type per mesh (don't hardcode UINT32) | `VulkanCommandContext.cpp:318` |
| medium | render/perf | Add a VkPipelineCache | `VulkanGraphicsPipeline.cpp:450` |
| medium | scale | Spatial culling structure (BVH/grid) | `VisibilitySystem.cpp:70-129` |
| medium | scale | Async asset loading off the main thread | `AssetManagerSingleton.cpp` |
| low | render/perf | Sort batches by pipeline/material | `RendererSingleton.cpp:97-101` |
| low | render/perf | Tighter pipeline barrier stage/access masks | `VulkanCommandContext.cpp:111-112` |
| large | scale | Job system + secondary command buffers + transfer queue | `VulkanContext.cpp:178-234` |

These complement the already-filed issues (#8 world-scoped caches, #11 `vkDeviceWaitIdle`, #12
release asserts, #13 EnTT observers) and the VULKAN_REVIEW candidates (resize, validation, device
selection). Say the word and I'll file the new ones and add them to the board.
