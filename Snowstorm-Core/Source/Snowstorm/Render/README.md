# Render: subsystem notes

Design decisions, measured numbers and known limits for the renderer and the texture cook. Read the section for the area you change, and update it in the same PR.

## Best-practices notes (#81)

`--strict` logs two recurring notes. Small host-mapped buffers are already pooled (`SmallBufferPool.hpp`, #78). A note that remains comes from a device-local or readback buffer, or a driver that requires a dedicated allocation. The eager `UNDEFINED -> SHADER_READ_ONLY` transition in `VulkanTextureView::CreateImageView` is load-bearing for Sampled render targets: temporal history is read before any pass writes it, and the bindless descriptor declares `SHADER_READ_ONLY`, so skipping it trades the note for a layout error. `vulkan.diag.alloc` names the buffers and textures behind each note. Run it with `--validation.extra` on a GPU before changing either path.

## Descriptor sets

Non-bindless `DescriptorSet`s allocate from `VulkanDescriptorAllocator`: pools shared by layout shape
(descriptor-type counts, not layout identity), 64 sets doubling to 1024 per pool, `FREE_DESCRIPTOR_SET_BIT`,
one retry on a fresh pool after OUT_OF_POOL_MEMORY/FRAGMENTED_POOL. Sets are freed `kRetireFrames` (3) frames
after destruction by `BeginFrame` (same call site as `VulkanBindlessManager::AdvanceFrame`), so destroying a
set no longer stalls the device. Pools live until `Shutdown`. The policy arithmetic is backend-free in
`DescriptorPoolPolicy.hpp` (Catch2-tested). Bindless keeps its own pool.

## Shader and acceleration-structure tuning CVars

All of these are `ReadOnly` (startup-only) and default to today's behaviour. `Scripts/speed-tune.py` searches them through `SS_*` env. None has run on a GPU.

- `render.shaders.opt` (default -1): dxc `-O0..-O3`, and -1 passes no flag (dxc's own -O3). Keyed into the `.spv` cache only when set, so default cache entries stay valid. `Scripts/cook-shaders.py --opt N` mirrors it so an RGA run can see the same SPIR-V. Ignored under `render.shaders.debug`.
- `render.fp16` (default on): off forces the fp32 path (no `SS_FP16`, fp32 neural weights) on fp16-capable devices. Gated in `VulkanRendererAPI::IsFloat16Supported`, so the permutation and the weight packing cannot disagree. `cook-shaders.py --no-fp16` mirrors it.
- `render.as.prefer_fast_build` (default off): BLAS and TLAS build with `PREFER_FAST_BUILD` instead of `PREFER_FAST_TRACE`.
- `render.as.compaction` (default off): BLAS builds add `ALLOW_COMPACTION`, read the compacted size back and copy into a smaller allocation when it is strictly smaller (`ShouldCompactAs`). One extra synchronous submit per BLAS. The TLAS is not compacted. The flag and decision rules are `Render/AccelerationStructureBuild.hpp`, tested in `AccelerationStructureBuildTests`.

- `render.wg.ao`, `render.wg.gi`, `render.wg.reflection`, `render.wg.shadow` (default 8): square compute tile side, 4, 8 or 16 (anything else is 8). The GI family is GI, GITemporal, GISpatialReuse and GIDenoise. The shaders say `[numthreads(SS_WG_X, SS_WG_Y, 1)]` with the default in `Include/Workgroup.hlsli`. `Render/WorkgroupSize.hpp` (`ClampWorkgroupSide`, `WorkgroupCount`, `AppendWorkgroupDefines`) feeds both the define and the C++ dispatch math, so they cannot drift. The default emits no define, so its `.spv` key, its cooked variant and the RGA baselines are unchanged (adding `Workgroup.hlsli` re-keys the include-header hash once). A non-default side is a new permutation that compiles at runtime. It is not in `shader-permutations.txt`. None of the shaders has `groupshared` memory or reads `SV_GroupThreadID`, which is what makes the size free to change. Check that before extending the knob to another shader. The other `numthreads` shaders (SSAO, SSR, SSGI, Neural*, IBL*, PathTrace, Metrics, Pick, OmmBake) are not a knob yet. Unverified on a GPU: nothing here has run, and a larger tile can cost occupancy on the RT passes.

## Texture block compression

`cook.textures.compress` block-compresses cooked textures, **on by default** after eyes-on confirmation on
Sponza (the metrics below cannot settle it: block artifacts on gradients are exactly what whole-image
numbers underweight, so the default was held off until someone looked). Turn it off to cook exact RGBA8
when a texture must not be quantized. Everything below was measured on Sponza's 72 textures, RX 9070 XT,
Debug.

**Encoding is chosen by ROLE, not by one global format.** Role is derived from the material slot a
texture is referenced through (`AssetManagerSingleton::RebuildTextureRoles`), rebuilt at registry load
rather than stored per asset, so it costs no registry migration. Two materials disagreeing about one
texture leaves it Unknown and the conservative colour encoding wins.

| role | format | why |
|---|---|---|
| albedo, opaque | BC1, 4 bpp (BC7 at `cook.textures.quality=1`) | 8:1, and what Unreal/Unity/Godot all default to |
| albedo, alpha | BC3, 8 bpp (BC7 at `cook.textures.quality>=1`) | BC1's 1-bit alpha decodes transparent black and bleeds halos. BC3 encodes in seconds where exhaustive BC7 takes minutes per 2K texture |
| tangent normal | **BC5**, 8 bpp | two independent 8-bit planes, z reconstructed in the shader |
| metallic-roughness, AO | BC1, 4 bpp | measured adequate, see below |
| sprite | RGBA8, one level, whatever `cook.textures.compress` and `.quality` say | exact texels. See the 2D render path |

**Mips are filtered in the colour space of the role** (`MipSpaceFor`, `Render/TextureCook.cpp`): albedo in
linear light (sRGB EOTF LUT, exact OETF on write), normals and masks as plain byte averages, alpha always
linear. A role-Unknown texture follows the caller's `srgb` flag. The cache blob header carries role, mip
space, compress flag with quality tier, and an encoder revision. A mismatch re-cooks.

**BC7 is an opt-in quality tier, not the default.** It is 8 bpp, the same as BC3 and double BC1, so
`cook.textures.quality=1` promotes opaque albedo to BC7 (51 MB -> 102 MB on Sponza, half the saving
given back) and colour with alpha from BC3 to BC7. The default tier encodes alpha as BC3 (squish
`kDxt5`): DirectXTex's default BC7 search is exhaustive and took minutes per 2K alpha texture, which
stalled Sponza's load at 91/94. BC7 therefore runs with `TEX_COMPRESS_BC7_QUICK` unless
`quality>=2`. The encoder is DirectXTex (MIT, vcpkg `directxtex`), CPU, called without
`TEX_COMPRESS_PARALLEL` because cook workers are already the parallelism. libsquish keeps BC1/BC3/BC5.
The quality tier is global. A per-texture override waits for `.meta` sidecars. The tier is part of
`TextureCookKey`, so flipping it re-cooks. The FLIP delta of `quality=1` is not measured yet.

**Normals are what the role split is for.** BC1 pushes a unit vector through one shared RGB565 line:
3.47 degrees of mean angular error against BC5's 1.13, over the 22 maps measured. The slot alone
cannot be trusted, so `IsTangentSpaceNormalMap` checks content (mean vector length <= 1.1, <= 5% of texels
with z<0) and falls back to colour encoding with a warning when it fails. Sponza's lion-head and shield
decals (mat23, mat3) are genuine normal maps whose padding is the (0,255,0) fill, which decodes to
(-1,+1,-1). The check skips that corner, so they encode as BC5. Before that skip they measured length 1.2-1.3
with 26-40% z<0 and warned.

**Masks stay BC1, measured rather than assumed.** On the channels the shader actually reads
(`mr.g` roughness, `mr.b` metallic) BC1 gives 44.7 dB and 44.0 dB, against normals' 31 dB that needed
fixing. The unused R channel has a source std of 1.2, so it is constant. Any 8 bpp alternative doubles
mask storage for a signal already fine, and a two-channel BC5 would be 8 bpp against BC1's 4, so BC1 is
also the efficient choice here, not merely the adequate one.

**What compression buys, both halves measured.** Cache 390 MB -> 69 MB. VMA allocation
**1373.3 -> 1074.8 MiB, 298.5 MiB less**, reproducible to the byte across runs. Interleaved A/B on one
binary with only the cache swapped puts the **Forward pass at 0.376 -> 0.292 ms on its per-run minimum,
-22%**, per-run ranges of 0.005 and 0.003 and no overlap, all four paired deltas sharing sign. The
bandwidth argument for BCn is therefore measured here, not inferred: block-compressed texels stay
compressed in VRAM and are decoded in the texture unit.

**What it costs.** Whole-frame FLIP against uncompressed is 0.026-0.028 across three viewpoints. For
scale, `all-rt` sits 0.103 from the path tracer, so this is not free. It was 0.045 before the role split
and the encoder-quality fix.

**Use the encoder's best fit.** The cook asks squish for `kColourIterativeClusterFit`, not
`kColourRangeFit` (its own header calls that one "very fast, low quality"), which is worth +2 dB across
every role at byte-identical output. A cook runs offline and should buy the quality.

**BC5 carries no third channel**, so every shader that samples a normal map decodes through
`DecodeTangentNormal` in `Engine/Shaders/Include/NormalEncode.hlsli`, discriminating on a raw blue below
0.5 (a stored tangent normal has z>0, which always encodes above it). That header declares no resources
on purpose: two of its three callers do not include `Engine.hlsli`, and a resource declared there is
emitted into every shader that includes it. **The path tracer decodes through the same helper**, since it
is the reference the quality gates compare against.

Sampling a BCn image requires `textureCompressionBC` **enabled**, not merely supported. It is requested
in `VulkanContext`. Without that the cooked textures are invalid usage that desktop drivers happen to
accept and validation does not flag.

## Directional shadows

`render.shadow.directionals` (default 1) is how many directionals get a raster shadow map. The primary (first
enabled) keeps the dedicated sun map. Up to 3 more get scene-fit tiles of one `DirAtlas` (2x2, each
`render.shadow.resolution`), selected by `SelectShadowDirectionals` in registry order and over-budget lights
render unshadowed with a one-shot warning. `GPUDirectionalLight::ShadowSlot` carries the choice to the shader
(-1 none, 0 sun map, s >= 1 `DirShadows[s - 1]`). Ray-traced shadows ignore the budget: every casting
directional traces. Secondary directionals still light the scene but are invisible to GI, reflections, the
path tracer, the sky disc and IBL, which read `Lights[0]` only. Each secondary costs one full scene depth pass.

`Render/ShadowAtlasMath.hpp` holds the pure tile math `ShadowRenderer` renders through for the directional, spot and point atlases (row-major `ShadowAtlasTileRect`: a directional with `ShadowSlot` s uses tile s-1, point face f of slot s uses tile s*6+f). `ShadowAtlasMathTests` pin it GPU-free, beside the slot rules in `PrimaryDirectionalTests`.

`Render/CascadeMath.hpp` holds the pure cascade math for a future sun CSM (#58): practical split scheme, slice corners, sphere fit and texel-snapped ortho matrix. `CascadeMathTests` cover it. `LightingSystem` runs `CascadeMath::ComputeFit` per frame for the primary directional behind `render.shadow.cascades.fit` (default off. `.count`, `.lambda`), using the primary perspective camera's unjittered view-projection, and hands the result to `RendererService::GetSunCascadeFit`. `render.shadow.cascades.render` (default off, needs `.fit`) adds a `CascadeShadows` pass that draws every caster into a 2x2 `CascadeAtlas` (`render.shadow.resolution` per tile, tile i = cascade i, `CascadeMath::AtlasTile`/`AtlasUvRect`), one depth pass per cascade without per-cascade culling, and publishes the atlas bindless index through `RendererService::GetCascadeAtlasIndex`. `render.shadow.cascades.shade` (default off, needs `.render`) appends a cascade block to FrameCB (`CascadeViewProj[4]`, `CascadeAtlasRect[4]`, `CascadeSplits`, `CascadeBiasScale`, `CascadeViewDepthRow`, `CascadeCount`, `CascadeAtlasIndex`. `MakeShading` packs it, offsets static_asserted) and `SampleSunShadow` then picks the first cascade whose split exceeds the fit camera's view depth (`SelectCascade` is the tested mirror), scales depth bias and normal offset by `CascadeBiasScale`, and shades from the atlas through `SampleShadowFactor`. Selection is hard: no blend band, no distance fade, and fragments past the last split are unshadowed. With it off the single sun map is used and the block is zero. Only the primary directional's `ShadowSlot == 0` path changes. RT shadows ignore it. Not run on a GPU: seam placement, bias and cost are unmeasured.

## Material shader parameters

A custom surface shader names the floats it packs into `PerInstanceCustomData` with `// @param` lines:
`// @param Zoom float slot=2 default=3 min=0.5 max=4` (types `float float2 float3 float4 int color3 color4`.
`slot` is the first vec4 component, default first free). HLSL packing cannot be named by SPIR-V reflection,
so the declaration is the contract. `ParseShaderParams`/`PackShaderParams` (`Render/ShaderParams.hpp`) are
the pure core. The material inspector draws one row per parameter (right-click resets), edits land in the
sparse `ShaderParams` map of the `.ssmat`, and `AssetManagerSingleton` packs them into the instance at
build time. A controller system that writes `SetPerInstanceCustomData` per frame still overrides them.

## Alpha mode

`MaterialAsset::Alpha` is `Opaque`, `Mask` or `Blend`, persisted as `"AlphaMode"` in `.ssmat` (the legacy boolean
`"AlphaMask"` is read, never written). The GPU layouts carry only the mask flag, set when the mode is `Mask`, so
`Blend` is opaque in every shader consumer (shadow maps, RT acceleration structure, path tracer) and is told apart
only on the CPU, by pipeline.

`render.transparent` (default off, #82) draws `Blend` materials in a sorted pass. Off, `Blend` renders as opaque as before.
On, `MaterialInstance::IsSortedTransparent` (CVar on, Blend, blend pipeline built) routes an instance through
`RendererService::DrawMeshTransparent` instead of a batch, and the camera depth prepass, DepthNormal and velocity passes skip it.
The prepass skip is required for correctness: glass depth there would reject everything behind it in the forward
`LessOrEqual` test. `AssetManagerSingleton` builds the per-shader blend variant (`GetOrCreatePipeline(frag, Blend)`: blend on, depth write off,
destination alpha kept. Cached beside the opaque one) and stores it on the `Material` (`GetBlendPipeline`).

`AddForwardPass` ends with the transparent flush: after the opaque `Flush` and the sky (the sky only fills uncovered
pixels), in the same render pass. Before it the GI, AO, reflection, sun-shadow and demodulated-specular indices are
zeroed, since those screen-space buffers describe the opaque surface behind the glass. The pane falls back to the
analytic terms. `FlushTransparent` sorts far-first with `SortTransparentDraws` (view-axis depth of the world-space bounds centre, ties by
submission order, `TransparentSortTests`) and merges adjacent runs of one (mesh, material) into a single instanced draw,
under a `Transparent` GPU scope that appears only when something is drawn. Sorting is per instance: it cannot order faces within one
mesh or between intersecting meshes. Blend writes no velocity, so TAA and the neural upscaler smear glass over a moving background. Shadow maps
and RT still treat glass as a solid caster. `quality-bench` compares against the path tracer, which renders `Blend` as
opaque, so keep `Blend` out of scenes with committed quality baselines.

**`AlphaBlend.world` is the testbed for the transparent pass.** Three overlapping half-alpha panes at distinct depths
(red near, green mid, blue far) in front of an opaque cube, a low-alpha blend cube, and a `Mask` pane (`Sponza_mat0`) with a
blend pane in front and one behind it. Run it with `--render.transparent`: every pane tints the ones behind it in depth order.
Without the flag the panes are solid and occlude. Alpha mode is an asset property, not an override, so the scene adds one
material, `materials/AlphaBlend.ssmat` (`"AlphaMode": "Blend"`), and varies colour and alpha per entity through
`BaseColor` overrides. It has no baselines. Not yet run on a GPU: the sorted pass is unverified by smoke, perf-bench or eyes.

`Render/EmissiveLightMath.hpp` holds the pure math for emissive-surface next-event estimation in the path tracer (#154): uniform triangle sampling, area-to-solid-angle pdf, the balance weight and a power CDF. `EmissiveLightMathTests` cover it. `render.pathtrace.emissivenee` (default off, #154) wires it into `PathTrace.comp`: `TlasBuildSystem` builds an `EmissiveLightList` (world-space triangles of every instance with a non-black emissive factor, CDF over area times factor luminance, 64-byte `GpuTriangle` records read by device address. `Mesh` keeps a CPU copy of positions and indices on RT devices for it) and stamps each `GeometryRecord.EmissiveFirstTriangle`. Each hit samples one triangle uniformly and balance-weights it against the BSDF-continuation emissive hit (whose light pdf is found from `EmissiveFirstTriangle - 1 + prim`), so camera rays keep full weight and the converged image is unchanged. Emission is two-sided, visibility is a hard occlusion test, textured emitters are picked by their factor only, and over `kMaxTriangles` (2^20) the list is dropped (warning, BSDF-only emission). Toggling it restarts accumulation and rebuilds the TLAS. Adds `PT_SLOT_EMISSIVE_*` Sobol slots, so sampler-1 streams for bounces after the first differ from earlier builds. `EmissiveLightListTests` pin the record layout and CDF packing. The shader compiles and was RGA-analysed offline but has not run on a GPU. `PathTrace.comp` worst case moved 107 to 108 VGPRs (gfx1100, same occupancy) and 95 to 100 (gfx1200/1201, 16 to 12 waves/SIMD), ISA +21%. The baselines carry the new figures.

## 2D render path

`CameraComponent::RenderPath` is `Scene3D` (default) or `Canvas2D` (#338), the Bevy `Camera2d` / Unity URP 2D Renderer
model: one ECS and one `TransformComponent` for both, separate render paths. A `Canvas2D` viewport runs `RenderCanvas2D`
and none of the Scene3D chain (no prepass, lighting, tonemap or TAA): one `Sprites` pass draws every
`SpriteComponent` into the present target, cleared to the camera's `BackgroundColor` through a second `RenderTarget`
object over the same image. It uses the unjittered `ViewProjection`. The frame-global IBL bake and shadow maps are skipped when no rendering viewport uses `Scene3D` (`NeedsSceneGlobalPasses`, `FrameGlobalPassGateTests`, #344). One Scene3D viewport next to a Canvas2D one keeps them. Frame-time effect on a 2D-only scene is unmeasured, and the gate has not run on a GPU.

Sprites outside the camera's x/y clip planes are dropped before the sort (`SpriteIntersectsView`, conservative: all four corners outside one plane, a corner behind the eye keeps the sprite, and depth is not tested). The gather stays a serial loop, not `ParallelGather`: it reads components through the registry and the per-sprite test is a few mat4 ops, unmeasured on a GPU or stress scene.

Draw order is `Layer`, then `Order`, then entity id, never z (`SortSpriteDraws`, `SpriteBatchTests`). `Size` is in world
units times the transform's scale. A flip mirrors about `Pivot`. Sprites are unlit and straight-alpha blended with the
destination alpha kept, since ImGui blends the viewport image by it. `Filter` picks a per-sprite point or linear sampler.
`OrthographicSize` is the full view height, not the half height.

`Rect` is a texel sub-rectangle (x, y, width, height, origin top-left, the order rows are stored): sprite sheets and tilesets
(Unity Sprite Editor slices, Bevy `TextureAtlas`). A zero width or height draws the whole texture. `SpriteUvRect`
(`SpriteBatch.hpp`, `SpriteBatchTests`) clamps it to the texture and maps it to the offset and scale carried in
`SpriteInstance::UvRect`, which `Sprite.vert` applies to the quad's UV. A rect outside the texture falls back to the whole texture.
`Size` is not derived from `Rect`, and packing and slicing UI are out of scope. A flip mirrors the quad, so it flips the cell.
The `Linear` filter samples across the cell edge with no padding, so bleed from neighbouring cells is possible. `Nearest` is exact.

`SpriteResolveSystem` resolves `Texture` to the runtime `View`. With `Texture` at 0 the system leaves `View` alone, which is
how a game binds a texture it writes. `ResolvedFrom` records which views the system set, so clearing `Texture` drops only
those. The sprite shaders include no `Engine.hlsli` and keep their resources in set 1, so they never touch the shared sets
0 and 2. All viewports' instances for a frame share one buffer per frame in flight, uploaded by the first sprite pass to
execute, with a push-constant base instance per viewport.

**Sprite textures cook as `TextureRole::Sprite`** (#339, Unity's Sprite texture type, Unreal's Paper2D texture
settings): `GetSpriteTextureViewAsync` requests the role rather than deriving it, and `PlanTextureCook` gives it one exact
sRGB level, never block-compressed, so the compress setting never re-cooks a sprite. The blob is `<handle>.sprite.sstex`
and the view sits in its own cache, so a texture used by a sprite and a material keeps both cooks and both GPU images.
With one level, a sprite drawn much smaller than its texture aliases. Mips and compression per asset wait for `.meta`.
`cook.assets` walks the registry, which cannot know scene usage, so sprite variants cook on first use.

The editor's Scene view takes the scene's Primary camera's render path and background on open (#341). A Canvas2D scene also
opens the **2D Scene view** (Unity's Scene-view 2D toggle, Godot's 2D editor): `CameraControllerRuntimeComponent::View2D` on the
editor camera, flipped by the viewport's "2D" button or the `2` key. `CameraControllerSystem` applies the transition (saving
the 3D pose and restoring it on the way out), then runs `Execute2D`: orthographic, rotation locked at zero, middle or right drag
pans, the wheel zooms about the cursor (`ViewportInteractionComponent::CursorUv`), right-held WASD pans, and F / Shift+F frame
the selected sprite / every sprite (`FrameEditorCamera2D`, `SpriteLocalAABB`). The maths is `Math/Scene2DView.hpp`
(`Scene2DViewTests`). Opening a non-2D scene leaves the mode and discards the saved pose. Sprites are not pickable by click yet
and the translate gizmo's behaviour under the ortho camera has not run on a GPU.
`quality.capture` also captures a Canvas2D viewport: `RenderCanvas2D` runs the same capture effect after the `Sprites` pass, with the same settle rule, writing `<path>_ldr.npy` from the present target. `_hdr.npy` is skipped (no linear scene colour, and the log says so). Not run on a GPU: that `Sprites.world` captures headlessly and matches a window capture is unconfirmed.

**Pixel-perfect camera (#340, PR 1).** `CameraComponent::PixelsPerUnit` > 0 on an orthographic `Canvas2D` camera (0, the default, is off) derives the view height from the viewport: `PixelPerfectScale` takes the largest integer at which `ReferenceResolution` fits (an axis of 0 does not constrain, no reference means 1, and it is never below 1), and `PixelPerfectOrthographicSize` makes `OrthographicSize = viewportH / (PixelsPerUnit * scale)`, so one texel spans exactly `scale` screen pixels. The camera translation is snapped to the texel grid (`SnapToTexel`) in `CameraRuntimeUpdateSystem`. Rotation is not handled. Unity's Pixel Perfect Camera and Godot's `viewport` stretch mode also render into a reference-size target and letterbox. Here the sprites still draw straight at present resolution, so the remainder is stretched, not letterboxed, and sprites keep `Nearest` filtering for the exact mapping. `PixelPerfectTests` cover the math. The system wiring and the shimmer-free pan are unverified on a GPU.

**`Sprites.world` is the testbed**, booted by smoke as `Sprites` (its camera is pixel-perfect at 16 PPU, 320x180 reference): a 64x64 checkerboard magnified with `Nearest` and with
`Linear`, a layer -1 logo 2 units nearer the camera that still draws under them, two layer 1 logos over them (one `FlipX`,
tinted at 0.7 alpha), and a rotated layer 1, order 1 checker over the logo it overlaps, and `Checker Rect`, the top-left 32x32 texels of the checker at 2x2 units. The `Rect` path (shader UV offset and scale, instance layout) has not run on a GPU.

## Subgroup size

`render.subgroup.size` (32 or 64, default 0 = driver's choice) and `render.subgroup.overrides` (`GIDenoise=32,Reflection=64`, substring of the shader path, first match wins, beats the global) set `requiredSubgroupSize` on every compute pipeline stage through Vulkan 1.3 `subgroupSizeControl`. Both are read at pipeline creation, so they apply at launch. A size outside the device's `[minSubgroupSize, maxSubgroupSize]`, or on a device without compute `requiredSubgroupSizeStages`, is dropped silently per pipeline (`ResolveRequiredSubgroupSize`, `SubgroupSizePolicyTests`). No shader uses wave intrinsics, so the size changes scheduling and occupancy only. A/B with `perf-bench.py --compare-exe`, passing the CVar through the environment of one arm. No result exists yet, and the build and run were not done on a GPU.

## Neural upscaler tail fusion

`neural.fuse` (default off) replaces the last conv layer and the residual add with one `NeuralConvAdd.comp.hlsl` dispatch when the last layer has 3 output channels (else the unfused chain runs): one dispatch, one barrier and one descriptor set fewer, and the residual map is never written to memory. It is a naive per-pixel loop at both precisions. `Conv2DResidualReference` is the CPU reference for the fused math (`NeuralMathTests` pins it to hand-computed values). It does not exercise the shader. Not run on a GPU: no unfused-vs-fused ms or output comparison exists yet. `NeuralConvAdd.comp` is RGA-baselined at 13 VGPRs, no spills, on every ASIC (#101). `Conv2DPairReference` is the oracle for the next fusion, two consecutive convs with the intermediate recomputed over the second layer's footprint and zero (not first(padding)) outside the image. No shader uses it yet. For the shipped 3/8 -> 16 -> 16 -> 3 model the candidate is the 16 -> 16 hidden layer into the 16 -> 3 tail: 9x recompute of the first layer's MACs per pixel against a 16-channel map no longer written or read, so it only pays if the pass is bandwidth-bound, which needs a GPU measurement.

## Shader hot-reload

`ShaderReloadSystem` (AssetSync phase) never compiles on the main thread. A source edit found by `ShaderLibrary::ReloadAll` and a DefaultLit permutation flip (`RequestRecompile`) both queue `Shader::Recompile()` on a JobSystem worker. The shader keeps its previous SPIR-V until the new result is published as one `CompiledShader` snapshot, and a failed compile publishes nothing, so the old pipeline keeps rendering. `CompileGate` coalesces overlapping requests into at most one live chain whose last run reads the latest inputs. `Shader::IsCompiling()` is true from queueing until `Recompile()` returns.

A permutation swap is tracked by `LitSwapState` (`Systems/ShaderReloadRule.hpp`, `ShaderReloadRuleTests`): `BeginLitSwap` records the version at the first request (a superseding request keeps it), `PollLitSwap` reports `Pending` while compiling, then `Succeeded` (version advanced: the pipeline sweep runs) or `Failed` (no version change: logged at `[error]`, old pipeline kept), once. `Pipeline::Reload` still drains the device (#11), which is now the remaining hitch. Not run on a GPU: the worker swap and the failure path are unverified outside the unit tests.

## IBL invalidation

The IBL bake is keyed on the environment block and on the sun (`Lights[0]`, the source `IBLBakePass` bakes).
`Render/IBLInvalidation.hpp` holds the pure rule: `SunNeedsRebake` compares against the last BAKED sun, re-baking
when its direction moves past `render.ibl.sun_quantum_deg` (default 2) or its colour*intensity changes by more
than 5%, or when the sun appears or disappears. A re-bake is still the full monolithic bake with a `WaitIdle`.
`IBLInvalidationTests` cover the rule (#149). Only the first bake computes the environment-independent BRDF LUT and reads back for the `.ssibl` cache (`PlanIBLBake`). Re-bakes skip both, so a sun sweep writes one cache file. Per-dispatch UBOs, sets and face views go into a `RetireQueue` freed `GetFramesInFlight()+1` frames later (`IBLBakePass::AdvanceFrame`), where they used to accumulate for the session. They are still allocated per bake. Not run on a GPU. Each re-bake after the first logs its `WaitIdle` stall, trigger and sun angle (`IBLRebakeStats`). Unobserved on a GPU.

`Lighting/LightPacking.hpp` is the pure encoding of light components into the GPU rows (`PackDirectionalLight`, `PackPointLight`, `PackSpotLight`: normalized direction, cone cosines with outer clamped to inner, the per-array caps). `LightPackingTests` pin it GPU-free.

A directional light that also has a `RotatorComponent` and a `TransformComponent` is turned by that transform (`LightingSystem`: authored `Direction` rotated by the entity's rotation), so `RotatorSystem` can sweep a sun. Every consumer (raster and RT shadows, sky disc, IBL bake via `Lights[0]`) sees the rotated direction. Without a Rotator, `Direction` is used as authored.

**`SunSweep.world` plus `camera-paths/sun-sweep.json` is the repro for #149's re-bake.** An open ground plane with six columns and three crates under the Sponza procedural sky, and one shadow-casting sun (`Direction` -x, slightly down) rotating about Z at 6 deg/s: dusk to noon in 15 s, setting after 30 s. At the default 2 degree quantum that is about three re-bakes a second, each a full monolithic bake with a `WaitIdle`, so frame-time spikes at that cadence are the symptom. The route is a 15 s lateral pass 4 m in front of the props at 1 m/s. Run with `--startup.scene=Projects/Sandbox/assets/scenes/SunSweep.world --camera.path --camera.path.file=Projects/Sandbox/assets/camera-paths/sun-sweep.json`, and compare `--render.ibl.sun_quantum_deg=10`. No baselines, and it has not been rendered or run on a GPU, and neither has the Rotator-driven sun path.

## Device capability dump

`vulkan.caps` (+ `vulkan.caps.path`) logs and writes what the driver advertises for `VK_KHR_cooperative_matrix` (every M/N/K, type and scope shape), shader float16/int8 and subgroup size control (`VulkanDeviceCaps`). It queries the physical device without enabling any extension, so it describes the adapter, not the engine's choices. `py Scripts/vulkan-caps.py --gpu <name>` runs it per adapter and prints the table. `Scripts/test_vulkan_caps.py` covers the formatting. Unrun on a GPU as of its PR, and no engine path uses cooperative matrix.
