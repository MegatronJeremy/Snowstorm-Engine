# Sorted transparency (experimental)

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.*

**Experimental and off by default.** `render.transparent` defaults to false (#82). Off, `Blend` materials render as opaque. No render of this pass is committed and it has not run on a GPU as of this page: no image or timing below is a measurement, and the sorted pass is unverified by smoke, perf-bench or eyes.

## Alpha modes

`MaterialAsset::Alpha` is `Opaque`, `Mask` or `Blend`, persisted as `"AlphaMode"` in `.ssmat`; the legacy boolean `"AlphaMask"` is read and never written. The GPU material layouts carry only the mask flag, set when the mode is `Mask`. `Blend` is therefore opaque in every shader consumer (shadow maps, the RT acceleration structure, the path tracer) and is told apart only on the CPU, by pipeline.

| Mode | Depth write | Sorted | Seen by shadows, RT, path tracer |
| --- | --- | --- | --- |
| `Opaque` | yes | no (batched) | as a solid |
| `Mask` | yes, alpha-tested | no (batched) | alpha-tested |
| `Blend` | no | yes, with `render.transparent` | as a solid |

## The pass

With `render.transparent` on, an instance whose material is `Blend` and has a built blend pipeline (`MaterialInstance::IsSortedTransparent`) goes to `RendererService::DrawMeshTransparent` instead of a batch.

- **Blend variant.** `AssetManagerSingleton` builds one per surface shader (`GetOrCreatePipeline(frag, Blend)`): blend on, depth write off, destination alpha kept. It is cached beside the opaque pipeline and stored on the `Material`.
- **Excluded from depth prepasses.** The camera depth prepass, DepthNormal and velocity passes skip these instances. The prepass skip is required for correctness: glass depth there would make the forward `LessOrEqual` test reject everything behind it.
- **Placement.** `AddForwardPass` ends with the transparent flush: after the opaque `Flush` and the sky (the sky fills only uncovered pixels), in the same render pass, depth test on.
- **Order.** `FlushTransparent` sorts far-first with `SortTransparentDraws`: view-axis depth of the world-space bounds centre, ties broken by submission order (`TransparentSortTests`). Adjacent runs of one (mesh, material) merge into one instanced draw. The cost shows as a `Transparent` GPU scope only when something is drawn.
- **Lighting inputs.** The GI, AO, reflection, sun-shadow and demodulated-specular indices are zeroed first, because those screen-space buffers describe the opaque surface behind the glass. A pane falls back to the analytic terms.

| CVar | Default | Effect |
| --- | --- | --- |
| `render.transparent` | off | Sorted Blend pass; off draws Blend as opaque |

`--list-cvars` is the authority if this table drifts.

## Testbed: `AlphaBlend.world`

`Projects/Sandbox/assets/scenes/AlphaBlend.world` holds three overlapping half-alpha panes at distinct depths (red near, green mid, blue far) in front of an opaque cube, a low-alpha blend cube, and a `Mask` pane (`Sponza_mat0`) with a blend pane in front and one behind it. Alpha mode is an asset property, so the scene adds one material, `materials/AlphaBlend.ssmat` (`"AlphaMode": "Blend"`), and varies colour and alpha per entity through `BaseColor` overrides.

```
Snowstorm-Runtime --startup.scene=Projects/Sandbox/assets/scenes/AlphaBlend.world --render.transparent
```

Expected, to be confirmed by eye: each pane tints the ones behind it in depth order. Without the flag the panes are solid and occlude each other. An on/off image pair from a named GPU is pending a GPU run and will be added with its GPU, commit and date.

## Limits

- **Per-instance sort only.** Faces within one mesh are not ordered, and intersecting meshes cannot be ordered correctly by a single depth per instance.
- **No velocity.** Blend writes none, so TAA and the neural upscaler smear glass over a moving background.
- **Glass is a solid to shadows and RT.** Shadow maps, the RT acceleration structure and the path tracer treat it as opaque, so glass casts a full shadow and is invisible to traced reflections and GI as glass.
- **No quality baselines.** `quality-bench` compares against a path tracer that renders `Blend` as opaque, so keep `Blend` out of scenes with committed quality baselines.
- **Not order-independent.** Weighted blended OIT and per-pixel linked lists are not built; sorted alpha is the smaller subset of that design.

Related issues: #82, #305.
