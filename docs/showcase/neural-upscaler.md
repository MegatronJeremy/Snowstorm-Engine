# Neural upscaler (experimental)

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.*

**Experimental, off by default, and no trained model ships.** `render.upscaler` defaults to 0 (bilinear) and `neural.weights` defaults to empty, which loads the built-in identity refiner. Trained `.ssnn` files and the dataset are gitignored, so every quality figure below comes from an artifact a reader cannot fetch from the repo. The one perceptual win is a lightly trained single-scene checkpoint; the spatial variant never beat bilinear.

When `render.scale` is below 1 the scene renders at that fraction of the viewport and is upscaled. The baseline is a bilinear pass. The neural alternative runs a small CNN as Vulkan compute passes on top of the bilinear image and adds its output back as a residual.

## Architecture

`NeuralUpscalePass` is a minimal inference runtime: a few primitive kernels chained from C++, with the network defined by the loaded `.ssnn` layer list, so changing the net changes weights, not the pass.

| Stage | Shader | Role |
| --- | --- | --- |
| Upsample | `NeuralUpsampleIn.comp.hlsl` | Bilinear upsample of the low-res colour to full res: feature channels 0..2, the residual base |
| Warp (temporal only) | `NeuralWarpHistory.comp.hlsl` | Reproject the previous full-res output by the velocity buffer into channels 3..5; write the motion vector into 6..7 |
| Conv, once per layer | `NeuralConv.comp.hlsl` | 3x3 "same" convolution with bias and optional ReLU, ping-ponging two feature maps |
| Residual add | `NeuralResidualAdd.comp.hlsl` | `output = bilinear + convStack(features)` into an RGBA16F storage texture, which tonemap then reads |

Two variants share the kernels:

- **Spatial** (`render.upscaler = 1`, #47): 3-channel input, the bilinear image only.
- **Temporal** (`render.upscaler = 2`, #98): 8-channel input, `[bilinear RGB, MV-warped previous output RGB, motion vector xy]`, the DLSS/XeSS pattern. It forces the velocity pass on and clears its history on a scene cut. `render.aa = 3` (DLAA) runs the same temporal network at native resolution as the temporal resolve, replacing TAA.

The model must match the selected mode: if the first layer's width does not equal the input width, the pass falls back to identity. The built-in identity refiner has a zero output layer, so the residual is 0 and the pass reproduces bilinear. That makes the untrained pass a provable no-op for validating the whole chain.

The PyTorch mirror is `ResidualRefiner` in `Tools/neural/model.py`. Its defaults are 32 hidden channels and 4 conv layers (three ReLU hidden layers and a linear output), all 3x3. The engine's C++ identity fallback is the smaller 3, 16, 16, 3 shape. Weights use the `[outC][inC][kH][kW]` layout, which is PyTorch's conv order, and the `.ssnn` writer is byte-parity tested against the engine's own `--neural.dump_identity` output (`Tools/neural/test_ssnn.py`).

## CVars

| CVar | Default | Effect |
| --- | --- | --- |
| `render.scale` | 1.0 | Internal render fraction, clamped to [0.25, 1.0]; the upscaler is active only below 1 |
| `render.upscaler` | 0 | 0 bilinear, 1 neural spatial, 2 neural temporal |
| `neural.weights` | empty | Path to a trained `.ssnn`; empty is the identity refiner; reloaded lazily when it changes |
| `neural.dump_identity` | empty | One-shot: write the identity refiner to this path and exit (parity reference) |
| `render.compare` | off | Split screen, upscaled on the left and full-res ground truth on the right |
| `dataset.export`, `dataset.jitter`, `dataset.export.warmup`, `render.gt.ssaa` | off, off, 60, 1 | Training-data capture: low-res colour, motion vectors and full-res ground truth tuples |

`--list-cvars` is the authority if this table drifts.

## Cost

On GPUs with `shaderFloat16` and 16-bit storage the conv shader uses a tiled path: an 8x8 group caches one input channel's halo tile in groupshared memory and accumulates a block of output channels in registers, with fp16 weights (`9fd7b53` tiling, `9035e05` fp16, 2026-08-02). Without fp16 it keeps the naive per-pixel loop, because the register array spilled and ran 2x slower than naive there. The comment in `NeuralConv.comp.hlsl` records the measurement: Debug (`-Od`) shaders, temporal upscaler at `render.scale` 0.5, 48-channel model, `NeuralUpscale` GPU time **195 ms naive, 84 ms tiled with fp16, 392 ms for the spilling tiled fp32 variant**. The GPU is not named there, and these are unoptimised-shader times, so they are not a performance claim for a shipping build. **No optimised-build cost on a named GPU is committed**, and the pass is not in the perf baselines. Fusing consecutive layers into one dispatch (#101, PR #276) and cooperative-matrix convolution (#137, blocked on a compiler upgrade) are open.

## What is measured against bilinear

The history is in the issues; none of the figures below carries a GPU name in the repo.

**Engine inference is faithful.** With a static camera, Sponza, `render.scale` 0.5 (#102, 2026-07-13): bilinear 32.59 dB PSNR, 0.9656 SSIM; neural with identity weights 32.59 dB, 0.9656. The first report of a 1.2 dB identity gap was a measurement bug (two processes on a moving camera path with variable dt sampled different poses), so the pass is bit-faithful to bilinear.

**The spatial refiner never beat bilinear.** The same static-camera run scored the trained spatial model at 31.66 dB and 0.9592. Selecting on training-crop PSNR had reported +1.5 dB over bilinear, but the same weights scored -0.85 dB on full frames, in Python and in the engine. With a strided held-out split the best validation gain was -0.06 dB at epoch 7, roughly identity, and validation PSNR fell monotonically with more training. The stated reason: a single-frame refiner on an already-bilinear image has no access to the detail that half-res rendering discarded, because that detail is temporal.

**The temporal refiner wins on LPIPS, not PSNR.** Commit `c4fa32e` (2026-07-14), full 149-frame held-out Sponza sequence, LPIPS-selected epoch-1 checkpoint:

| Metric | Bilinear | Temporal neural | Reading |
| --- | --- | --- | --- |
| LPIPS (AlexNet, lower is better) | 0.1319 | 0.1005 | gain 0.0314, about 24% lower |
| PSNR | not recorded | 0.15 dB below bilinear | essentially tied |

An in-engine fixed-dt A/B with `render.upscaler = 2` ran clean, with PSNR tracking bilinear at about 44 dB. The GPU is not recorded. This is the selection trap in [`AGENTS.md`](../../AGENTS.md#selecting-a-learned-technique-denoiser-upscaler): PSNR structurally favours the blurry baseline, so a model that injects real sub-pixel detail is punished by PSNR whenever the detail lands fractionally off the ground-truth grid. Selection and loss are LPIPS (`L1 + lpips_weight * LPIPS` in `train_temporal.py`), and both numbers are reported with the perceptual one deciding. Select on held-out full frames, never training crops.

## Limits

- **Overfit, one scene.** Training used 119 frames of Sponza only. Held-out LPIPS was best at epoch about 1 (0.061) and worsened to about 0.099 by epoch 40 while training loss fell from 0.22 to 0.057 (#110). More data (#109) and training-side stability fixes (#110) are open; neither has a result.
- **The history is 3-channel RGB**, the net's own previous output. A wide learned-feature history as in DLSS or XeSS (#111) is not built.
- **Glass smears.** Blend materials write no velocity, so the temporal upscaler and TAA smear transparent surfaces over a moving background.
- **No quality gate.** The upscaler is not in `quality-bench.py` or `quality-motion.py`, and `render.compare` is the only in-engine A/B.
- **No eyes-on claim.** The "perceptually sharper under motion" reading rests on LPIPS; no side-by-side image is committed.

## Reproduce

Capture with `dataset.export`, `dataset.jitter`, `render.scale=0.5`, `render.compare` and `camera.path`, then `Tools/neural/train_temporal.py` (it needs torch and lpips, see `Tools/neural/requirements.txt`), then `eval.py` for the full-frame comparison. Run the result with `--render.scale=0.5 --render.upscaler=2 --neural.weights=<path>.ssnn`.

Related issues: #98, #99, #101, #102, #109, #110, #111, #135, #137.
