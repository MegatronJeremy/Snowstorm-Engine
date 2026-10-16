# RT global illumination

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza).*

`render.gi.mode` selects the diffuse GI technique: 0 off (default), 1 SSGI (screen-space, any GPU), 2 ray traced (hardware ray query; falls back to off on a device without ray-query support). Both producers write the same half-resolution irradiance target and share the temporal, a-trous and upsample tail, so the two A/B on one scene. GI needs TAA (`render.aa`) for a clean result, since few rays are traced per frame.

![Indirect diffuse term at the atrium viewpoint: bounce light from the red, green and blue drapes onto the floor and columns](../images/debug-gi.jpg)

The isolated indirect term. Light bounced off the drapes tints the floor and column bases; the unlit arcades stay dark because only one bounce is gathered.

## Technique

Per half-resolution pixel the GI pass reconstructs the receiver's world position from the G-buffer depth, reads its geometric normal and traces `render.gi.rays` (default 2) cosine-weighted hemisphere rays against the scene TLAS, limited to `render.gi.range` (default 17 world units). Each closest hit is shaded through `ShadeSurfaceHit` (`Engine/Shaders/Include/RTHitShading.hlsli`): sun with a shadow ray, emissive, and image-based ambient scaled by `render.gi.bounce_ambient` (default 0.5, so the sky is not double-counted at every bounce). Secondary hits are Lambertian only, with no specular lobe. The pass outputs incoming irradiance, not multiplied by albedo: the multiply happens at full resolution in the forward pass, so the upsample never blurs albedo edges.

The raw trace is then cleaned by the SVGF-style tail shared with SSGI:

1. temporal accumulation (`render.gi.temporal`, history weight 0.9 moving to 0.97 when static);
2. a-trous denoise (`render.gi.denoise.iterations`, default 3, variance-guided by `render.gi.denoise.variance`);
3. depth-aware bilateral upsample to full resolution (`render.gi.scale`, default 0.5).

`render.gi.spec_ambient_fade` (default 1) fades the un-occluded environment specular by roughness while GI is on, so rough surfaces do not carry a second unoccluded ambient over the occluded diffuse GI.

### SSGI

`render.gi.mode` 1 marches the depth buffer along hemisphere directions (32 fixed steps across `render.gi.range`) and gathers the previous frame's lit colour. It costs a fraction of the trace and needs no RT hardware, but sees only what is on screen from the previous frame, so off-screen and occluded bounce light is missing.

## Quality against the path tracer

Source: committed baselines `Scripts/quality-baseline/<adapter>/{atrium,floor,gallery}__{raster,ssgi,rtgi}.json`, commit `b253f91`, 2026-09-08. Mean over the three Sponza viewpoints; FLIP lower is better, PSNR and SSIM higher. Only GI is toggled, so the rest of the lighting is the same on every row.

| Technique | GPU | FLIP | PSNR (dB) | SSIM |
| --- | --- | --- | --- | --- |
| raster (no GI) | RX 9070 XT | 0.4352 | 15.14 | 0.3823 |
| SSGI | RX 9070 XT | 0.3373 | 17.65 | 0.4599 |
| RT GI | RX 9070 XT | 0.1395 | 25.36 | 0.7212 |
| raster (no GI) | RTX 5070 | 0.4345 | 15.18 | 0.3835 |
| SSGI | RTX 5070 | 0.3405 | 17.58 | 0.4579 |
| RT GI | RTX 5070 | 0.1409 | 25.22 | 0.7191 |

Per viewpoint FLIP on the 9070 XT (atrium, floor, gallery): raster 0.3631, 0.4081, 0.5343; SSGI 0.2352, 0.3065, 0.4701; RT GI 0.1436, 0.1296, 0.1452. Indirect light is the largest single term missing from the raster frame: RT GI removes about two thirds of the FLIP error against the path tracer, SSGI about a fifth. The two GPUs agree within 0.004 FLIP, as expected for a technique measured against a path trace on the same card.

## Cost

Source: committed baselines `Scripts/perf-baseline/<adapter>/{+refl,+gi,ssgi}.json`, commit `b253f91`, 2026-09-08. Sponza at 1920x1080, 300 frames per run, median of 5, `totalGpuMs`. The `+gi` rung adds GI on top of shadows, AO and reflections; `ssgi` repeats that rung with the screen-space producer, so both diff against `+refl`.

| | RX 9070 XT | RTX 5070 |
| --- | --- | --- |
| `+refl` rung | 7.274 ms | 4.742 ms |
| + RT GI | 8.991 ms (+1.717) | 6.155 ms (+1.413) |
| + SSGI | 8.037 ms (+0.763) | 5.662 ms (+0.920) |
| RT GI own passes (GI, GITemporal, GIDenoise0-2, GIUpsample) | 1.502 ms | 1.133 ms |
| SSGI own passes (SSGI, same tail) | 0.613 ms | 0.615 ms |
| of which the trace (`GI` / `SSGI`) | 1.069 / 0.193 ms | 0.721 / 0.216 ms |

The own-pass rows are the per-pass `avgMs` sums from the same files; the rest of each delta lands in `Forward`, which reads the GI target. The tail costs about 0.43 ms (9070 XT) and 0.41 ms (5070) whichever producer feeds it. RT GI costs about 2.2x SSGI on the 9070 XT and 1.5x on the 5070 for roughly a 2.4x lower FLIP, so on this scene the traced producer is the better trade wherever ray queries are available.

## ReSTIR variant (experimental, off)

`render.gi.restir` (startup-only, default off) replaces the average of the gather rays with a resampled reservoir: one sample plus its unbiased contribution weight, reused temporally (`render.gi.restir.temporal`) and spatially (`render.gi.restir.spatial`, `.radius`, `.count`, optional reconnection visibility ray `.spatial.visibility`). It is a separate shader permutation (`SS_GI_RESTIR`) because compiling the reservoir path into the default shader cost the default path 22 VGPRs (85 to 107) and two waves of occupancy.

It is not a shipping path: it measures worse than the average it replaces at every capture window from 6 frames to converged, worst on the gallery viewpoint (FLIP 0.1747 to 0.1805 at 6 frames, 0.1781 to 0.1795 converged, against a 0.0002 noise floor; figures from the CVar description in `EngineCVars.cpp`, not a committed baseline). A reservoir carries one sample whose variance does not fall with M, while the default averages two fresh rays into a tuned denoiser chain, a stronger baseline than the 1 spp path trace ReSTIR is usually posed against. Its GPU cost is not measured.

## Limits

- One bounce, Lambertian hits only; metal-on-metal interreflection is absent (the path tracer has it).
- Range-limited: a ray that exhausts `render.gi.range` lands in the sky miss branch, so too short a range leaks unoccluded sky into closed interiors. 17 improved FLIP on all three viewpoints (mean -9.6%) over the earlier default.
- Half-resolution trace; thin features rely on the depth-aware upsample.

## Reproduce

```
py Scripts/perf-bench.py --only +gi
py Scripts/perf-bench.py --only ssgi
py Scripts/quality-bench.py --only rtgi
py Scripts/quality-bench.py --only ssgi
```
