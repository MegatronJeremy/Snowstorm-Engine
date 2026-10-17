# Dynamic-sun IBL re-bake

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.*

The IBL maps (irradiance, prefiltered specular, BRDF LUT) are convolved from the procedural sky plus the sun, so a moving sun leaves ambient lit from where the sun used to be. `RenderSystem` therefore compares the sun against the one the maps were baked with and re-bakes when it has moved enough (#149). **No image or timing below is a measurement:** the rule is covered by unit tests, the testbed scene has not been rendered on a GPU, and the cost of a re-bake is not measured.

## The rule

`SunNeedsRebake` (`Render/IBLInvalidation.hpp`, GPU-free) compares the current sun against the **last baked** sun, never the previous frame's, so a slow drift accumulates until it crosses the threshold instead of being lost in per-frame steps. The key is `Lights[0]` only, the same source `IBLBakePass` bakes: direction (normalised, surface to sun) and radiance (`Color * Intensity`).

| Change since the baked sun | Re-bake |
| --- | --- |
| Sun appears or disappears (no lights, or a zero direction, is "no sun") | yes |
| Direction moved more than `render.ibl.sun_quantum_deg` (default 2) | yes |
| `Color * Intensity` changed by more than 5% of the baked magnitude | yes |
| Anything smaller | no |

`render.ibl.sun_quantum_deg = 0` re-bakes on any direction change. The 5% radiance tolerance is a constant at the call site, not a CVar. A changed environment block (sky colours, intensity) invalidates the maps through the existing environment comparison, independent of the sun.

## What a re-bake costs

`Invalidate()` followed by a new bake is the full monolithic path, not an incremental update: `RenderSystem` calls `Renderer::WaitIdle()` before appending the bake passes, so every re-bake stalls the GPU once. The disk cache (`Engine/cache/ibl/<hash>.ssibl`) is keyed by a hash of the sky and the lights, so a sun position that was baked before is uploaded instead of re-computed. Inference from the code, not observed: a continuous sweep hashes to a new key at nearly every re-bake, so it mostly misses and writes one cache file per baked angle.

The quantum is the whole budget control. At 2 degrees, a sun turning at 6 degrees per second re-bakes about three times a second (arithmetic, not a measured rate). Raising the quantum trades ambient accuracy for fewer stalls; the right value for a given scene is not measured.

## Testbed: SunSweep.world

`Projects/Sandbox/assets/scenes/SunSweep.world` (#310): a small room of crates under the procedural sky with one shadow-casting directional light, 8.0 intensity, warm white, starting at direction (-1, -0.35, 0). A `RotatorComponent` turns the light about +Z at 6 degrees per second (#310 teaches `RotatorSystem` to turn a directional light). `camera-paths/sun-sweep.json` is a five-waypoint route past the crates.

```
Snowstorm-Runtime --startup.scene=Projects/Sandbox/assets/scenes/SunSweep.world \
  --camera.path --camera.path.file=Projects/Sandbox/assets/camera-paths/sun-sweep.json
```

Add `--render.ibl.sun_quantum_deg=30` for a visibly stale ambient (re-bake every 5 s at this speed, arithmetic), or `--render.ibl.sun_quantum_deg=0` for the per-change extreme. Expected, not observed: with a small quantum the shadowed side's ambient follows the sun; with a large one it lags in steps. The scene has no baselines and is not wired into any gate.

| CVar | Default | Effect |
| --- | --- | --- |
| `render.ibl` | on | Bake and use IBL from the sky; off falls back to analytic hemisphere ambient |
| `render.ibl.sun_quantum_deg` | 2 | Degrees of sun movement from the baked sun before a re-bake |
| `render.ibl.intensity` | 0.75 | Multiplier on the IBL ambient |

`--list-cvars` is the authority if this table drifts.

## Checks

`IBLInvalidationTests` (Catch2, no GPU) cover the key reading `Lights[0]` with the direction negated, no lights and a zero direction both meaning no sun, movement under and over the quantum, drift accumulating against the baked key, the colour and intensity tolerance, the sun appearing and disappearing, and quantum 0.

Not checked: that the sweep re-bakes at the expected rate in a running engine, the stall length per re-bake, the effect of the quantum on frame time, image difference between quantum values, and the cache growth described above. A measured sweep from a named GPU is pending a GPU run and will be added with its GPU, commit and date.

## Limits

- **Stall per re-bake.** The full bake plus `WaitIdle` is a hitch, not a streamed update.
- **One sun.** Only `Lights[0]` is keyed; moving a secondary directional re-bakes nothing.
- **Sky is not time-of-day driven.** The procedural sky colours are authored values that nothing ties to the sun (read from the scene and environment component, not tested), so a sweeping sun re-lights an unchanged sky.
- **Cache growth.** Every distinct sun key can leave a cache file (see above, unverified).

Related issues: #149, #310, #315.
