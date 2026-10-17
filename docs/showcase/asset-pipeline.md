# Asset pipeline

*Design and measurements adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza) and the engine's own [`AGENTS.md`](../../AGENTS.md), which is the authority where they differ.*

Source files (`.obj`, `.png`, `.jpg`, shaders) are cooked once into GPU-ready artifacts under `Engine/cache`. Scenes and materials name assets by virtual path, never by where the file sits on disk. Textures are block-compressed by role. The reference model is the Unity importer and Unreal's cook: a source asset, an importer, a cooked artifact, a stable name.

## Cook cache

Meshes, textures, shaders and IBL cook to `Engine/cache`, keyed on a **content hash**; mtime is only a cheap pre-gate, so touching a file without changing it re-cooks nothing. The cache blob header of a texture carries its role, mip space, compress flag and an encoder revision; a mismatch re-cooks, so changing the encoder or the quality tier invalidates exactly the textures it affects (`TextureCookKey`).

Cooking is loading: `CookAllRegistryAssets` (`--cook.assets`, or *File > Cook Assets*) loads every mesh, texture and material the registry names through the same path a running scene uses, so the two cannot disagree. A lazy cache holds only what some run touched; a full cook is what a package needs.

```
cmake --build build --config Debug --target stage   # package: cooked cache is generated, not staged
py Scripts/smoke-test.py --cold --scene <Sponza.world> --timeout 480
```

A cold Debug cook of Sponza's 72 textures is the slow case: the smoke harness gives the Editor a 480 s settle budget for it.

## Virtual paths

`VirtualPath` is a mount table with three mounts and no way to add more:

| Mount | Points at | Writable |
| --- | --- | --- |
| `/Engine/` | `Engine/Shaders`, `Engine/Fonts` | no |
| `/Game/` | the active project's assets | no |
| `/Cache/` | generated artifacts | **yes, the only writable mount** |

The single writable mount is Godot's `res://` against `user://`: it keeps a sealed, read-only content layout possible without auditing every file-open. Resolution is longest-prefix-wins, a pure function of the table (Unreal's insert-at-head shadowing depends on registration order). It never probes the filesystem: a missing `/Game/x.png` is an error, not a search under `/Engine/`. `AssetRegistry`'s `Canonicalize` gives an asset exactly one spelling, so a mounted, an absolute and a legacy project-relative path reach one handle. Without that, every content-browser scan re-imported every asset under a new handle and the registry grew by its own size per run (`AssetIdentityTests`). Multi-mesh files address parts as `path?submesh=N`.

Not built yet: `.meta` sidecars with a GUID separate from the path, hot-reload, async streaming.

## Texture compression by role

`cook.textures.compress` (default on) chooses the encoding from the material slot a texture is referenced through, rebuilt at registry load:

| Role | Format | Why |
| --- | --- | --- |
| albedo, opaque | BC1, 4 bpp (BC7 at `cook.textures.quality=1`) | 8:1 against RGBA8 |
| albedo, alpha | BC3, 8 bpp | BC1's 1-bit alpha bleeds halos |
| tangent normal | BC5, 8 bpp | two independent 8-bit planes, z rebuilt in the shader |
| metallic-roughness, AO | BC1, 4 bpp | 44.7 dB on roughness, 44.0 dB on metallic |

Mips are filtered in the role's colour space (albedo in linear light, normals and masks as byte averages). A texture sitting in a normal slot is checked by content (`IsTangentSpaceNormalMap`): two Sponza images there are not normal maps (vector length about 1.3, 25-40% of texels with z<0), so they fall back to colour encoding.

### Why normals are not BC1

Source | BC1 round trip | 4x absolute error, 128 px crop at 3x:

| A Sponza tangent-space normal map | A Sponza colour texture |
| --- | --- |
| ![Normal map, its BC1 round trip and 4x error: the error is dense and chromatic](images/bc1_normal_map.jpg) | ![Colour texture, its BC1 round trip and 4x error: the error is nearly black](images/bc1_colour.jpg) |
| 28.25 dB over the full 1024x1024 texture | 42.45 dB over the full 1024x1024 texture |

Produced by `py Scripts/showcase-bc1-figure.py <texture> <out.jpg>` on 2026-10-17 from `Projects/Sandbox/assets/meshes/Sponza/6593109234861095314.jpg` and `755318871556304029.jpg`. This is an **offline numpy encoder** (PCA-axis fit with least-squares endpoint refinement), not the engine's libsquish cluster fit, and the PSNR is RGB against the source, not a render. It illustrates the mechanism: one shared RGB565 line cannot carry a unit vector, while a colour texture compresses almost invisibly. It is not the engine's output and not a GPU capture.

The engine's own measurement over the 22 conforming normal maps: mean angular error **3.47 degrees with BC1, 1.13 with BC5**.

### Measured effect

All on Sponza's 72 textures, Radeon RX 9070 XT, Debug build, recorded in `AGENTS.md` by commit `32135a8` (2026-09-08):

| Measure | Uncompressed | Compressed | Delta |
| --- | --- | --- | --- |
| Cooked cache | 390 MB | 69 MB | -82% |
| VMA allocation | 1373.3 MiB | 1074.8 MiB | -298.5 MiB, reproducible to the byte |
| Forward pass GPU time (per-run minimum) | 0.376 ms | 0.292 ms | -22% |

The Forward figure is an interleaved A/B on one binary with only the cache swapped: per-run ranges 0.005 and 0.003 ms, no overlap, all four paired deltas the same sign. The cost is quality: whole-frame FLIP against uncompressed is **0.026-0.028** across three viewpoints (0.045 before the role split and the encoder fix); for scale, `all-rt` sits 0.103 from the path tracer. Default-on followed eyes-on confirmation on Sponza. BC7 (`cook.textures.quality=1`) doubles opaque albedo to 8 bpp (51 MB to 102 MB on Sponza); its FLIP delta is not measured.

The encoder asks libsquish for `kColourIterativeClusterFit`, worth about +2 dB on every role at byte-identical output over `kColourRangeFit`. Sampling a BCn image needs `textureCompressionBC` enabled, not merely supported.

## Reproduce

```
py Scripts/Generate-Solution.py && cmake --build build --config Debug
Snowstorm-Editor --cook.assets
Snowstorm-Runtime --cook.textures.compress=0   # uncompressed A/B arm; re-cooks on the key change
py Scripts/perf-bench.py --compare-exe <ref-exe> --pairs 3
```
