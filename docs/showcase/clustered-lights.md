# Clustered light culling (experimental)

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.*

**Experimental and off by default** (#66). Both CVars default to false. The grid math and the CPU assignment are covered by unit tests; the shade path has not run on a GPU as of this page. No image or timing below is a measurement, and no light-count scaling curve exists yet.

## Cluster grid

`ClusterGrid` (`Render/LightClusterMath.hpp`, GPU-free) divides the view frustum into a 3D grid. View space has +z as positive distance along the view direction.

- **Tiles:** 64 px square. `TilesX/TilesY` round up, and the last row and column are clipped to the render target, so tiles cover the screen with no overhang.
- **Slices:** 24 exponential depth slices between the camera's near and far planes (Olsson et al. 2012). The near face of slice k is `Near * (Far / Near)^(k / Slices)`; depths outside `[Near, Far]` clamp to the first or last slice.
- **Index:** `(slice * TilesY + y) * TilesX + x`.
- **Bounds:** `ClusterBounds` gives the view-space AABB of a cluster for a symmetric perspective projection.

## Assignment

`AssignClusterLights` builds the per-cluster lists on the CPU. Each point and spot light is a view-space range sphere, tested against every cluster AABB with `SphereOverlapsAabb`; touching counts as overlapping, so the cull is conservative. A spot's cone is ignored for the same reason.

- **Order:** ascending light index within a cluster.
- **Stride:** fixed (`render.lights.cluster.max`, default 32). A light past the stride is dropped from that cluster and the cluster counts as overflowed.
- **Cost:** brute force, O(clusters x lights) on the main thread each frame. It is the reference for a future compute cull, not a production path.
- **Scope:** the presented perspective camera only. Directional lights are not clustered.
- **Statistics:** `RendererService::GetClusterStats()` holds clusters, occupied clusters, the largest list, overflowed clusters and the light count. Nothing displays them yet.

## Shade path

`render.lights.cluster.shade` implies the assignment and uploads the lists to DefaultLit.

- **Layout:** `PackClusterLightLists` writes `Counts[ClusterCount]` followed by `Indices[ClusterCount * Stride]` into a per-frame-in-flight storage buffer. Cluster c's list starts at word `ClusterCount + c * Stride`.
- **Binding:** DefaultLit reads the buffer through its device address (`vk::RawBufferLoad`), so no descriptor layout changes. The address, grid and `ClusterViewDepthRow` ride a FrameCB tail.
- **Lookup:** the shader repeats `ClusterIndexFromPixel` (`ClusterIndexOf`, with `SliceScale = Slices / log(Far / Near)`). The C++ function is the tested mirror; the two must be kept in step by hand.
- **Result:** the point and spot loops visit only the cluster's lights, in the same ascending order as the full loops, so pixels match the unclustered result unless a cluster overflows. Directional lights, sampling and every other term are unchanged.
- **Applies only to:** the presented camera at a scene-target size equal to the viewport. With `render.scale != 1`, and in every other pass, the loops run over all lights.

| CVar | Default | Effect |
| --- | --- | --- |
| `render.lights.cluster` | off | Build the lists each frame and publish their statistics; changes no pixels |
| `render.lights.cluster.shade` | off | DefaultLit shades from the lists; implies the assignment |
| `render.lights.cluster.max` | 32 | Lights kept per cluster; overlapping lights past it are dropped and counted |

`--list-cvars` is the authority if this table drifts.

## Overflow rule

A dropped light is not shaded in that cluster, so an overflowing cluster is the one case where clustered and unclustered pixels differ. The drop is silent in the image; `Overflowed` in the cluster statistics is the only signal. Raise `render.lights.cluster.max` until it reads 0 for the scene in question.

## Checks

`LightClusterMathTests` (Catch2, no GPU) cover grid dimensions, slice monotonicity and clamping, abutting slice depths, tiles covering the screen, a projected point landing in the cluster its pixel and depth select, sphere-AABB edge cases, ascending order and overflow, agreement of the assignment with the per-cluster reference cull, the packed layout, a fragment's cluster listing every light whose sphere contains it, and `ClusterIndexFromPixel` clamping.

Not checked: the DefaultLit shader against the CPU mirror on a GPU, pixel equality with the unclustered path, frame cost, and behaviour at high light counts. A measured on/off comparison from a named GPU is pending a GPU run and will be added with its GPU, commit and date.

## Limits

- **CPU assignment per frame.** The O(clusters x lights) cost grows with both, so this is not yet a win at large light counts.
- **Range spheres.** Spot cones are not tested, so spot lists are larger than necessary.
- **One camera.** The lists are valid for the presented camera at one scene-target size.
- **Symmetric perspective only.** Orthographic cameras skip the assignment.

Related issues: #66, #312.
