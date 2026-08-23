# AGENTS.md: Snowstorm Engine

3D game engine with an abstraction over the rendering backend. Currently Vulkan-only (DirectX 12
planned). Windows-only for now. Public domain (`UNLICENSE.txt`). The engine is Hazel-inspired
(`Ref`/`Scope`, `Layer`/`LayerStack`, `SS_*` macros, instrumentation profiler) but has grown its
own EnTT-based ECS, a Systems/Singletons/Services architecture, a Vulkan RHI, an asset system, and
an ImGui editor.

> This repo is consumed as a git **submodule** of `MegatronJeremy/RG2`, but it has its own
> independent history and `master` branch. Develop it as a standalone project. The working language
> here is **English** for everything: code, comments, commit messages, issues, and chat. (The RG2
> parent asks for Serbian commits and chat; that governs RG2's own contents, not this repo.)

## Agent collaboration

How to work in this repo. These rules live here rather than in a personal config so that a clone on
any machine, driven by any agent, behaves the same.

- **Review in chat before push, and push is gated on explicit approval.** For every change, show the
  diff or the key snippets with enough surrounding context to read like a real PR: what changed, why
  it was done that way, the tradeoffs, non-obvious invariants, and what remains unverified. A local
  commit before that review is fine; the gate is on `git push`. The same gate applies to anything
  else outward-facing or hard to reverse (issue comments, remote branches, force pushes).
- **One increment per step.** Each step leaves the repo building and runnable. Small reviewable
  changes beat big batches.
- **Prefer a short plan before touching a risky surface**: descriptor/binding layouts, serialization
  formats, shared or global state, public APIs. Two minutes of planning catches the class of bug that
  otherwise only surfaces after a full build and test cycle.
- **Verify NUMBERS, not just actions.** Never state a measurement, benchmark, or quantitative result
  that was not actually produced. If a measurement is unfinished, say so; a fabricated number is
  worse than "not measured yet". Every summary separates what was built, run, and measured from what
  is inference, and marks the inference as such.
- **Flag conflicts of interest.** When reviewing work you produced earlier (your own code against an
  external PR, your own benchmark against someone's claim), say so, so the verdict can be weighted.
- **Assume deep fundamentals.** The reader is a graphics/systems engineer, so skip tutorial framing
  on C++, Vulkan, and rendering basics. Terse and correct beats verbose and hedged.

### Writing: comments, docs, commits, issues

- **Maximal information, minimal text.** Cut every word that adds none: no throat-clearing, no
  filler, no restating the obvious. Keep the quantitative (numbers, names, counts) and the
  qualitative (why, tradeoff), and cut the connective overhead between them.
- **Committed artifacts state durable facts, not status.** No work log, no TODO, no "as of this
  change" framing. Ephemeral status belongs in the issue tracker, where it is meant to change; in a
  file it rots and forces a churn commit to correct it.
- **Comments: default to none.** Names carry WHAT. Comment only a non-obvious WHY that the code
  cannot express: a hidden constraint, a load-bearing invariant or ordering, a workaround, or domain
  encoding a reader cannot derive from the code (wire-format bit ops, protocol field numbers, bit
  layouts). Cold-reader test: would a competent reader who does not already hold this domain in their
  head be lost here? Prefer a structural fix (named constant, better name, small helper) over a
  comment. Never restate the signature, narrate the line, or reference a ticket or a caller.
- **No em-dashes, anywhere.** The glyph is not the whole problem: the tell is clauses joined by
  adjacency instead of by a stated relationship, so deleting the character alone relocates the habit
  into comma splices and stacked parentheticals. Name the relationship instead. Use a colon if the
  second half explains the first, "although" or "since" if it qualifies, parentheses for a genuine
  aside, a semicolon between two independent clauses. A plain hyphen dash is fine.
- **Revise, do not just constrain.** Run a second pass over your own draft: with the whole clause in
  context the connective can match how the sentence actually ended, which an upfront ban cannot do.
  Strip the other AI-writing tells on that pass (the "not just X, but Y" antithesis, rule-of-three
  lists, hollow puffery).
- **Keep contributor workflow out of consumer-facing docs.** `README.md` is for someone using the
  engine; build, test, and release process lives next to the code it concerns.

## Build & run

Toolchain: **CMake + vcpkg**, generating a Visual Studio 2022 solution (toolset `v143`).
Everything is driven by `Scripts/Generate-Solution.py` (bootstraps vcpkg under `vcpkg/`, installs
all packages, configures CMake into `build/`):

```
# from repo root (or double-click Scripts/Generate-Solution.bat, which cd's up a level first):
py Scripts/Generate-Solution.py                 # default triplet x64-windows -> build/Snowstorm.sln
py Scripts/Generate-Solution.py --clean         # wipe build/ first
py Scripts/Generate-Solution.py --fresh         # also wipe vcpkg installed/buildtrees (full reinstall)
```

**MSVC toolset is detected, not hardcoded.** vcpkg and CMake pick a toolset by different rules
(vcpkg takes the latest installed minor version; a bare `-T v143` takes the VS instance default),
and when they diverge Catch2 fails to link with LNK2019 on vectorized-STL symbols. The script
resolves the latest installed toolset once via `vswhere` and gives the same version to both: CMake
via `-T v143,version=<ver>`, vcpkg via the overlay triplet in `Scripts/vcpkg-triplets/` (a copy of
stock `x64-windows` plus `VCPKG_PLATFORM_TOOLSET_VERSION`, read from `SS_MSVC_TOOLSET_VERSION`). It
also pins `CMAKE_GENERATOR_INSTANCE` so a multi-VS box can't resolve the version against a different
install. Override with `SS_MSVC_TOOLSET=<ver>`; detection failure falls back to a bare `-T v143`
with a warning. Because the triplet feeds vcpkg's ABI hash, **changing the resolved toolset
invalidates installed packages**: re-run with `--fresh` when a toolset change forces a rebuild.

Then open `build/Snowstorm.sln` and build. **Snowstorm-Editor** is the default startup project;
the debugger working directory is the repo root, so relative `Engine/...` and `Projects/...` paths
resolve. Vulkan
validation layers are wired via the `VK_ADD_LAYER_PATH` env var (set by the script and in the VS
debugger environment) pointing at the vcpkg `bin` dir.

The first run is slow: vcpkg compiles every dependency from source.

### New box (one time)

A clone plus these commands reproduces the full workflow. There is deliberately no setup script:
this is six commands, and each is documented in its own section below.

```
git clone https://github.com/MegatronJeremy/Snowstorm-Engine && cd Snowstorm-Engine
git config core.hooksPath .githooks              # arms the format pre-push hook (committed but inert until set)
pip install clang-format==22.1.5 numpy flip-evaluator==1.7
py Scripts/Generate-Solution.py                  # bootstraps vcpkg; the first run compiles every dependency
cmake --build build --config Debug
py Scripts/smoke-test.py
```

Requires Windows and Visual Studio 2022 (`Generate-Solution.py` detects the exact `v143` toolset
version). No Vulkan SDK installation is needed: loader, headers, and validation layers all come from
vcpkg. `Generate-Solution.py` runs `vcpkg integrate install`, a machine-global MSBuild side effect
outside the repo.

The Python packages serve the gates, not the engine: `clang-format` at the pinned version for the
pre-push lint hook, `numpy` as a hard requirement of `quality-bench.py`, `flip-evaluator` for the
perceptual metric (optional, although its absence is now reported rather than silently skipped).

**The golden-file gates start empty on a new machine.** Perf and RGA baselines are keyed by adapter
and ASIC, and quality baselines are captured against a path trace on the local GPU, so none of the
committed sets apply until this box captures its own:

```
py Scripts/perf-bench.py --update-baseline       # only if this adapter has no committed set
py Scripts/cook-shaders.py --variants rt,base    # both permutations, matching what CI cooks
py Scripts/rga-occupancy.py --spv-dir Engine/cache/shaders-cook
```

Until a baseline exists, those gates exit **2** (SKIP), never 0, so an unmeasured machine cannot
report a pass. Commit a new adapter's perf and RGA baselines. Quality baselines are the exception:
they are committed but not device-keyed by path, so on a different GPU the gate reports SKIP and
`--update-baseline` would overwrite the existing machine's set. Re-capture locally, keep it out of
the commit, or move the reference machine deliberately.

## Smoke test (run after non-trivial changes)

`Scripts/smoke-test.py` boots each executable headlessly and checks it doesn't crash or log
errors. It launches every app with `SS_SMOKE_FRAMES` set (the engine then runs that many frames
and exits cleanly), captures stdout/stderr, enforces a per-app wall-clock timeout (a hang/deadlock
becomes a failure instead of blocking), checks the exit code, and scans the log for error markers
(`[error]`/`[critical]`, Vulkan validation, assertion text). Exit 0 = all pass.

```
py Scripts/smoke-test.py                 # 120 frames, 60s timeout/app, Debug build
py Scripts/smoke-test.py --frames 300    # longer soak
py Scripts/smoke-test.py --only Editor   # single target (Editor | Runtime)
py Scripts/smoke-test.py --warnings-fail # treat [warning] lines as failures too
py Scripts/smoke-test.py --strict        # enable deeper Vulkan validation (see below)
```

`--strict` sets `SS_VALIDATION_EXTRA=1`, which enables **synchronization validation** (barrier/
semaphore/fence hazards) and **best-practices** (perf/usage foot-guns) via `VkValidationFeaturesEXT`.
These are off by default since they add overhead and best-practices is advisory/noisy. Strict findings
are logged at `[warning]` level and shown as **notes**, not failures (a strict run still PASSes on
them); add `--warnings-fail` to gate on them. Genuine validation **errors** are always `[error]`
level and fail the run in either mode. **GPU-assisted validation** is separate and much heavier
(it instruments shaders to catch out-of-bounds descriptor/buffer access at execution time): it sits
behind its own `validation.gpu` CVar (`SS_VALIDATION_GPU=1`), off by default and not implied by
`--strict`.

**Run it after any change substantial enough to affect runtime behavior** (engine/render/ECS/asset
code, the frame loop, anything touching Vulkan), and not for docs/comment/build-script-only edits.
Build first (`cmake --build build --config Debug`), then smoke-test. It needs a **real GPU/display**
(Vulkan), so it is a **local** gate that cannot run on hosted CI; the GitHub `build` workflow
compiles and runs the GPU-free unit tests (`ctest --test-dir build -C Debug --output-on-failure`),
nothing that needs a device. The harness sets `VK_ADD_LAYER_PATH` itself so validation layers load.

The harness also sets `SS_VALIDATION_NONFATAL=1`: by default the Vulkan validation messenger
asserts (and the process dies) on the first ERROR, so you only see one error per run. With this env
var set, every validation error is logged and the app keeps running, so a single smoke run surfaces
**all** of them at once: the harness then detects failures by scanning the log, not the exit code.
Set it yourself when debugging validation interactively. GPU resources are also named via
`SetVulkanObjectName` (`VK_EXT_debug_utils`), so validation/RenderDoc report e.g. `Swapchain[0]`
instead of a raw `VkImage 0x...` handle.

**Smoke and perf-bench do not cover the unit tests.** Both boot the app and watch it behave, so
neither notices a broken exact-string expectation: `PerfBenchTests` asserts the JSON the benchmark
writes, character for character. Run `ctest --test-dir build -C Debug --output-on-failure` (or the
`Snowstorm-Tests` target) before pushing any change to a serialization or report format, since CI
runs it and a green smoke run says nothing about it.

## GPU perf benchmark (run before/after any render-path change)

`Scripts/perf-bench.py` is the GPU analogue of the smoke test: a golden-file microbenchmark gate.
It runs the Editor headlessly once per **RT-effect config** (via `perf.bench.frames` /
`SS_PERF_BENCH_FRAMES`), each of which averages the render graph's per-pass GPU timings over a fixed
frame budget and writes a JSON (`PerfBench.hpp` builds it; `Application::Run` drives it past a 15-frame
warmup that also covers the 1-frame timestamp lag). The script parses each JSON, prints a per-pass
table, and **diffs against a committed baseline** in `Scripts/perf-baseline/`, failing (exit 1) if any
pass regresses beyond `--threshold` (default 15%).

```
py Scripts/perf-bench.py                    # run the matrix, diff vs baseline, PASS/FAIL
py Scripts/perf-bench.py --update-baseline  # capture current results as the new baseline
py Scripts/perf-bench.py --only +gi         # one config (rt-off | shadows | +ao | +refl | +gi | ssgi)
py Scripts/perf-bench.py --frames 300       # more frames = less noise, slower
py Scripts/perf-bench.py --gpu 5070         # pin the adapter on a multi-GPU box
```

The config matrix (`rt-off → shadows → +ao → +refl → +gi`) enables one RT effect at a time, so the
**Forward-pass ms delta between adjacent configs is that effect's cost**: the RT effects are inline in
the Forward pass, so this A/B *is* the per-effect timing (there's no separate GPU scope per effect, by
design). A trailing `ssgi` config sits outside that ladder, repeating the `+gi` rung with the
screen-space GI producer, so it diffs against `+refl` rather than its neighbour and gives the
screen-space-vs-RT cost of the same effect. Sub-0.05 ms passes are ignored (timestamp noise). Like
smoke, it needs a **real GPU** (Vulkan timestamps) so it's a **local** gate, not CI; on a device
without timestamp support the JSON sets `timestampsSupported:false` and the script skips rather than
false-failing.

**Baselines are keyed by adapter**: `Scripts/perf-baseline/<device-slug>/<config>.json`, where the slug
comes from the device name the run reports. GPU differences make ms non-comparable, so a run only ever
diffs against the set captured on the adapter it is running on, and a box with two cards keeps two
independent sets (the repo holds `amd-radeon-rx-9070-xt`, `nvidia-geforce-rtx-5070`, and an
`amd-radeon-rx-7900-xtx` set from another machine). Cross-vendor comparison is then a deliberate read
across two directories rather than an accidental mixture inside one set, which would turn every
cross-config delta into effect-cost plus hardware difference. Missing set = the gate prints the raw
numbers and exits **2** (SKIP), which is not a pass: it compared nothing, so it cannot claim one.
On a multi-GPU machine `SS_CONFIG_IGNORE` also discards the persisted
`render.gpu` pick, so selection falls back to auto and the adapter is whatever the driver enumerates
first; `--gpu` pins it, taking a short all-digits value as a candidate **index** and anything else
(including a model number like `9070`) as a case-insensitive name substring. Re-baseline deliberately
(with a commit) when a change *intends* to shift perf, never to paper over an unexplained regression.

**A baseline is also implicitly keyed by resolution**, since the Editor renders at the window size.
A regression that moves *every* pass by roughly the same factor is a different monitor or window
size, not a code regression: check the resolution before touching the numbers, and never re-baseline
to make that shape go away.

**Nondeterministic GPU numbers across runs point at the shader cache first.** Clear
`Engine/cache/shaders/*.spv` and re-run before trusting any before/after comparison; a stale cache
means the run measured a mixture of old and new shaders. If the numbers still move after a clean
cache, that is a real race and a genuine finding.

## Shader occupancy gate (RGA, static)

`Scripts/rga-occupancy.py` is the static analogue of perf-bench: a golden-file gate on shader
register/LDS pressure and spills, the determinants of GPU occupancy. It needs no GPU run. It feeds
every compiled SPIR-V module in `Engine/cache/shaders/` through the Radeon GPU Analyzer offline
compiler for a target ASIC (default `gfx1100`, the RX 7900 XTX), parses the per-shader stats CSV
(USED_VGPRs/SGPRs, USED_LDS_BYTES, VGPR/SGPR spills, SCRATCH_MEM, ISA_SIZE), collapses each shader's
permutations to the worst case keyed by base name (so a source edit re-compares the same logical
shader, not a churning content hash), and diffs against `Scripts/rga-baseline/occupancy-<asic>.json`.

```
py Scripts/rga-occupancy.py                    # analyse cache, diff vs baseline, PASS/FAIL
py Scripts/rga-occupancy.py --update-baseline  # capture current results as the new baseline
py Scripts/rga-occupancy.py --only Reflection  # one shader (base-name substring)
py Scripts/rga-occupancy.py --livereg --only GIDenoise  # pinpoint the peak live-VGPR instruction
py Scripts/rga-occupancy.py --dry-run          # print planned RGA invocations, don't run RGA
```

`--livereg` is an investigation mode (not gated): it runs RGA live-VGPR analysis and prints the
instruction holding the most live registers, the actionable target for cutting a shader's VGPR count
(e.g. GIDenoise.comp peaks at 184 live VGPRs around a `v_cndmask` block).

The **primary gate is VGPR-limited occupancy**: RGA's CLI has no occupancy column, so the script
derives waves/SIMD from the VGPR count using the RDNA3 (gfx11) model (1536 VGPRs/SIMD, 16 waves max,
<=96 VGPRs => full 16 waves), verified against AMD's docs. It fails (exit 1) when occupancy drops
(fewer waves), a spill appears (0 to >0, hard fail), or LDS/ISA rises beyond `--threshold`
(default 10%). Raw VGPR% is intentionally not gated -- a VGPR rise that doesn't cross a wave boundary
costs nothing. The occupancy is *theoretical* and VGPR-only (LDS occupancy needs the workgroup size
RGA offline reports as 0); measure achieved occupancy with RGP. On the current baseline only
`GIDenoise.comp` (192 VGPR -> 8/16 waves) is occupancy-limited; every other shader hits 16/16.
Stage per module is read from the SPIR-V `OpEntryPoint` execution model, not
the filename, so the stage-less `IBL*.hlsl` shaders resolve correctly. RGA is **pinned** to a version
+ SHA-256 in the script (its stats columns and compiler drift between versions, like the clang-format
pin) and **auto-bootstraps**: if not found via `--rga` / `SS_RGA` / PATH it downloads the pinned
Windows build into `Tools/rga/` (~238MB, one-time, cached, gitignored), so a fresh box is fully
headless. The one honest limit: this gates *theoretical* register/occupancy, not *achieved* occupancy
/ bandwidth / stalls (measure those with RGP, runtime capture, headless via `RadeonDeveloperPanelCLI`),
and the offline compiler can differ slightly from the live driver. Re-baseline deliberately (with a
commit) when a change intends to shift register pressure.

It exits **2** (SKIP), not 0, whenever a shader was never actually compared: no baseline for this
ASIC, a shader new since the baseline, or a baselined shader missing from the analysed SPIR-V. That
last case is the runtime-cache trap below, and it is the common one: an editor run only caches the
shaders it used, so the default invocation silently skipped 14 of the baselined shaders and still
printed PASS before the exit code was split. `--only` narrows the baseline to the same filter, so a
spot check compares its one shader and stays a genuine pass.

**Input SPIR-V (two sources, one baseline).** Locally the gate reads `Engine/cache/shaders/`,
populated by any editor build+run. But a clean checkout / CI has no cache and no GPU, so
`Scripts/cook-shaders.py` compiles every shader offline with the engine's exact dxc flags
(VulkanShader.cpp: profiles `vs/ps/cs_6_5`, entry `main`, `SS_RAYTRACING`/`SS_FP16` permutations)
into `Engine/cache/shaders-cook/`. The cook reproduces the runtime-cache numbers **bit-for-bit**, so
both paths gate against the same committed baseline; the baseline is generated from the cook so it
also covers shaders never exercised in a run (`Metrics.comp`, the `Neural*.comp` passes). The
baseline holds the worst case across **both** permutations, so reproduce it the way CI does, with
both variants and the cook directory:

```
py Scripts/cook-shaders.py --variants rt,base
py Scripts/rga-occupancy.py --spv-dir Engine/cache/shaders-cook
```

Cooking the default `rt` variant alone drops the `base` permutation from every entry, so any shader
whose worst case lives there is re-baselined to a weaker number and the local gate stops agreeing
with CI. Pointing the gate at the runtime cache (`Engine/cache/shaders`, its default) is the other
trap: a `--update-baseline` from there deletes the entries for shaders no editor run exercises. Keep
`cook-shaders.py`'s flags in sync with VulkanShader.cpp: it is a deliberate second copy of them, with
no shared source of truth. `cook-shaders.py` also replaced the stale `check_shaders.py` (which still
expected the old `#type` split and silently compiled nothing).

**This gate runs in CI** (`.github/workflows/shaders.yml`): unlike smoke-test and perf-bench, RGA is
static and needs no GPU, so hosted CI cooks the shaders and runs the occupancy gate on every shader
change. RGA is cached across runs (actions/cache) so only the first pays the download.

## Image-quality gate vs the path tracer (run after any change to a lighting technique)

`Scripts/quality-bench.py` is the correctness counterpart to perf-bench: perf-bench answers "how
fast", this answers "how close to ground truth". Per viewpoint it captures the **converged path
tracer** as the reference, then each real-time technique, and reports **FLIP** (perceptual, lower is
better), **PSNR** and **SSIM** of technique vs reference, diffing against
`Scripts/quality-baseline/<viewpoint>__<technique>.json` and failing (exit 1) past `--threshold`
(default 10%). This is how real-time GI/denoiser work is measured in the literature (SVGF, ReSTIR,
NVIDIA FLIP).

```
py Scripts/quality-bench.py                    # capture ref + all techniques, diff vs baseline
py Scripts/quality-bench.py --update-baseline  # capture current metrics as the new baseline
py Scripts/quality-bench.py --only ssgi        # a single technique
py Scripts/quality-bench.py --ref-frames 400   # PT accumulation frames (reference convergence)
py Scripts/quality-bench.py --fresh-ref        # ignore the cached PT reference and re-capture
```

Both sides tonemap through the same LDR chain, so toggling `render.pathtrace` is an apples-to-apples
A/B: the engine's headless quality-capture (`quality.capture.frames`) dumps the final present to
`<path>_ldr.npy` and the script computes the metrics offline. The reference runs **unbiased**
(`pathtrace.clamp` and `pathtrace.weightclamp` both 0).

- **Runtime, not Editor.** Captures run `Snowstorm-Runtime` (fixed viewport, no editor panels), so
  the framing is deterministic. Even so, every capture is bilinear-resampled to a canonical
  **1024x576** before any metric, because the editor viewport size is not reproducible across
  launches (1177x649 and 1817x1009 were both observed in one session) and a shape mismatch silently
  breaks the comparison. A fixed-resolution render path would remove the resample.
- **Three Sponza viewpoints** (`atrium`, `floor`, `gallery`) share one position and vary orientation,
  covering content that stresses AO, GI and reflections differently. Two further candidate
  orientations were dropped as degenerate (yaw+pi was 99.7% dark; the side yaw 78.7% dark, 0%
  bright). Averaging across viewpoints is what `Scripts/quality-tune.py` minimizes, so a tuned
  parameter does not overfit one frame.
- **Eight techniques**: `raster`, `ssao`, `rtao`, `ssr`, `rtrefl`, `ssgi`, `rtgi`, `all-rt`. Each is a
  full real-time render with that one technique on, TAA enabled.
- **FLIP is pinned** to `flip-evaluator==1.7` (a perceptual metric that drifts between versions makes
  a committed baseline meaningless, same reasoning as the clang-format and RGA pins). It is optional:
  without the package the gate still runs on PSNR/SSIM, although it then reports the primary metric
  as ungated (exit 2) rather than passing on the two secondary ones. **numpy is required.**
- The PT reference is cached under `Scripts/.quality-ref-cache/` (gitignored, per-machine). The cache
  key does **not** track engine/shader edits, so use `--fresh-ref` after a change that could move the
  reference itself. Real-time captures are frame-capped by `--tech-maxframes` (default 200) since
  they never converge; the reference is uncapped because it does.
- Needs a **real GPU** (Vulkan + the path tracer), so this is a **local** gate, not CI. **Baselines
  are keyed by adapter**, `Scripts/quality-baseline/<device-slug>/<viewpoint>__<technique>.json`, the
  same scheme perf-bench uses. The reference is a path trace on the local adapter, so a baseline
  captured on a different GPU measures hardware difference on top of technique error: a second card
  gets its own directory, and `--update-baseline` there cannot overwrite the first one's committed
  numbers. A missing set exits **2** (SKIP) and refuses to gate, as does a directory hand-copied from
  another machine (each JSON also records its device, checked against the running one).
- **A metric A/B needs a static camera.** Leave `--camera.path` out of any run whose numbers are
  compared against another run: it desynchronises which frame each side captures and manufactures
  differences that have nothing to do with the change under test. The gate's fixed viewpoints
  (`camera.override`, `SS_CAMERA_OVERRIDE`) exist for this reason and pin an arbitrary interior pose
  headlessly.

Every run sets `SS_CONFIG_IGNORE=1`, so a capture is code defaults plus the technique's overrides and
never this machine's persisted `SnowstormConfig.cfg`. Note that only `all-rt` overrides
`render.shadows.mode`: every other technique renders with the default shadow map, so shadowing is a
constant in the matrix rather than something the gate measures.

### Selecting a learned technique (denoiser, upscaler)

Two selection traps, both of which produced a wrong verdict here before being caught:

- **Select on held-out full frames, never on training crops.** A model can improve crop PSNR while
  losing on the full frame it will actually run on.
- **PSNR structurally favours the blurry baseline**, so it is the wrong selection metric for anything
  whose job is detail: a spatial refiner cannot beat bilinear on PSNR because the detail it must
  recover is temporal, not present in the input frame. Select on a perceptual metric (LPIPS here,
  which is also what DLSS is tuned against). Under LPIPS selection and LPIPS training, the temporal
  network beats bilinear by 0.0314; under PSNR selection the same network looks like a loss.

Report both anyway. The point is which one decides.

## Console variables (CVars)

Engine flags go through a small CVar registry (`Snowstorm/Utility/CVar.hpp`) instead of ad-hoc
`std::getenv`. Declare engine-wide CVars in `Snowstorm/Core/EngineCVars.{hpp,cpp}`; each
self-registers and is resolved once at startup by `CVarRegistry::Initialize(argc, argv)` (called in
`EntryPoint.hpp`) from, in increasing priority: **default → `SnowstormConfig.cfg` →
`SnowstormStartup.cfg` → environment → CLI**. `SnowstormConfig.cfg` is auto-saved by the editor on
shutdown and holds persistent CVars only; `SnowstormStartup.cfg` is hand-authored, never written by
the app, and can carry any CVar, so it is the safe place for machine-local toggles the auto-save
cannot clobber. `config.ignore` (`SS_CONFIG_IGNORE=1`) skips both files, which is what the benchmark
harnesses set so a run depends on code defaults plus its own overrides, never on local settings.

A CVar named `validation.extra` is set by env `SS_VALIDATION_EXTRA` **or** CLI `--validation.extra`
(dots→`_`, uppercased, `SS_` prefix for env). Bools accept presence (`--flag`, or env set to
anything but `0`/`false`/`off`/`no`). Run any executable with `--list-cvars` (or `--help`) to print
every CVar with its value, type, env name, and description. `EngineCVars.cpp` declares ~124 of them
(rendering technique modes, denoiser knobs, path tracer, benchmark hooks, validation); treat
`--list-cvars` as the authoritative list rather than any enumeration here. Startup resolution runs
once, but CVars can also be **edited live at runtime** from the editor's *Debug > Console Variables*
panel (`CVarPanelSystem`): it lists every CVar with a type-appropriate widget (checkbox/int/float)
plus a `name value` command line, via typed accessors on `ICVar` (`GetKind`/`Get*`/`Set*`). Most
engine CVars are read per-frame through `.Get()` (shadows, IBL, exposure, shadow quality), so edits
take effect immediately, and those marked `CVarFlags::Persist` are written back to
`SnowstormConfig.cfg` on shutdown.

## Layout

```
Snowstorm-Core/      # STATIC library: all engine code (the only place most work happens)
  Source/Snowstorm/  #   platform-independent engine (Core, ECS, Render, Systems, ...)
  Source/Platform/   #   Vulkan/ (RHI implementation, ~28 files) and Windows/
Snowstorm-Editor/    # Editor EXECUTABLE, links Core; ImGui dockspace, panels, viewport
Snowstorm-Runtime/   # Editor-free runtime EXECUTABLE, links Core; shares RegisterCoreSystems
Snowstorm-Tests/     # Catch2 unit tests (GPU-free; run by ctest, gated in CI)
Engine/              # engine-owned runtime data: Shaders/, Fonts/, and the gitignored cache/
Projects/Sandbox/    # the sample project: assets/ (scenes, meshes, materials, textures, registry)
Dataset/             # gitignored capture output + trained weights
Scripts/             # build, smoke, perf, quality, shader-occupancy tooling + committed baselines
Tools/dxc/           # DirectX Shader Compiler (HLSL -> SPIR-V)
```

Core builds to a static lib holding code shared by multiple apps; executables (currently the Editor)
link Core and add it to their include path. All targets are **C++20** (the root `CMakeLists.txt`
sets C++17 globally, although every target overrides to 20, so treat the project as C++20).

**Keep the in-editor shortcut reference current.** The editor has a *Help > Keyboard & Mouse
Shortcuts* window (`Snowstorm-Editor/Source/System/EditorMenuSystem.cpp`, `DrawShortcutsWindow`)
that documents every keyboard/mouse binding. Whenever you add, remove, or change a shortcut (camera
controls, gizmo keys, framing, save, selection, hierarchy actions, …), update that window in the
same change so the docs never drift from the real bindings. It is the single source of truth users
see, so treat it as part of the feature, not an afterthought.

## Architecture (Core)

- **Entry point:** clients define `Snowstorm::CreateApplication()`; `Core/EntryPoint.hpp` provides
  `main` (inits logging, wraps `Run()` in profiler sessions). `Application` owns the window, the
  `LayerStack`, the `EventBus`, and the `ServiceManager` (singleton via `Application::Get()`).
- **ECS:** EnTT-backed. `World`/`Entity` (`World/`), components in `Components/`, behavior in
  `Systems/` (managed by `SystemManager`), cross-cutting state in `Singletons/` (`SingletonManager`),
  and `Service/` for longer-lived services. Components self-register for reflection via **RTTR** plus
  the editor/serializer registry from a per-component static initializer (`RTTR_REGISTRATION { ... }` +
  `AUTO_REGISTER_COMPONENT(T)` in each `Components/*.cpp`; see `Components/ComponentRegistry.hpp`).
  Core is a static lib, so the executables link it `WHOLE_ARCHIVE` to keep those initializer TUs.
- **Data-parallelism is a first-class option for systems: always consider it.** When adding a new
  system (or extending/reworking one), explicitly ask whether its per-entity work is *pure and
  independent* (each entity reads/writes only its OWN components, no shared accumulator, no renderer/
  asset-manager/singleton calls, no `TrackedRegistry` mutation APIs in the loop) and would benefit from
  running across `JobSystem` workers. If so, use the existing primitives instead of a hand-rolled serial
  loop: `System::ParallelForEach<Read<T>/Write<T>...>` for in-place per-entity updates (the DOTS
  IJobEntity model; RotatorSystem is the reference), or `JobSystem::ParallelGather<T>(count, body, emit)`
  for parallel filter/collect into a list (VisibilitySystem's frustum cull is the reference). Both take
  a grain size, degrade to an inline serial pass for small N (parallel only when it pays), gate on the
  `ecs.parallel` CVar for a pure serial-vs-parallel A/B, and preserve deterministic (bit-identical)
  output so `ChangedView`/draw order stay stable. Most systems will NOT qualify (they submit to the
  renderer, touch singletons, run scripts, or scatter into shared state), and those stay serial on plain
  `System`, and that's the correct call, not a missed optimization. The point is to make the
  parallel-vs-serial decision *consciously* each time, not default to serial by habit. Note the current
  ceiling: the O(n) post-barrier change-mark (#91) caps end-to-end speedup at scale even when the
  compute parallelizes near-linearly, so measure with `--ecs.benchmark` rather than assuming a win.
- **Rendering:** backend-agnostic interfaces in `Render/` (`RendererAPI`, `Renderer`, `Pipeline`,
  `Shader`, `Buffer`, `Texture`, `Material`, `RenderGraph`, ...). The concrete implementation lives
  in `Platform/Vulkan/` (volk + Vulkan Memory Allocator + spirv-reflect; shaders compiled to SPIR-V
  via `Tools/dxc`).
- **Scenes:** serialized to/from JSON (`World/SceneSerializer.hpp`, nlohmann_json).
- **Events:** `Events/` hierarchy dispatched through `EventBus`; input bridged in `Input/`.

### Conventions

- Namespace `Snowstorm`. Smart-pointer aliases `Ref<T>` (shared) / `Scope<T>` (unique) with
  `CreateRef` / `CreateScope`. Use these, not raw `std::shared_ptr`/`make_unique`, in engine code.
- Macros from `Core/Base.hpp`: `SS_ASSERT` / `SS_CORE_ASSERT`, `BIT(x)`, `SS_BIND_EVENT_FN(fn)`,
  `SS_DEBUGBREAK()`. Logging is `SS_CORE_*` / `SS_*` (spdlog). Asserts compile out unless `SS_DEBUG`;
  use `SS_VERIFY` / `SS_CORE_VERIFY` for checks that must survive release builds.
- Platform code goes behind `SS_PLATFORM_WINDOWS` (see `Core/PlatformDetection.hpp`); the engine
  currently `#error`s on non-Windows.
- Headers are `.hpp`, translation units `.cpp`. Core globs all sources recursively, so a new file
  under `Snowstorm-Core/Source/` is picked up after re-running CMake (re-generate the solution).
- **Formatting (format-on-touch):** the repo has a `.clang-format`. The `lint` CI checks the C++
  files changed by a push/PR and **fails if any touched file isn't fully clang-format-clean**, so the
  codebase formats gradually as files are edited. Pinned to **`clang-format==22.1.5`** (match it
  locally, since version drift changes output). Run `clang-format -i <files>` (or enable format-on-save
  against the repo config) before committing. `Scripts/check-format.py` predicts this gate locally:
  default mode checks the files changed vs `master` + uncommitted (the same set CI gates on), `--all`
  scans the whole project (surfaces the legacy backlog CI does *not* gate on), `--fix` reformats in
  place. A tracked `pre-push` hook (`.githooks/pre-push`) runs the default mode so a lint-failing push
  is blocked before it leaves the machine. **Enable it once per clone** with
  `git config core.hooksPath .githooks` (bypass a single push with `git push --no-verify`).
- **Shared-header shader bindings are global: mind `space1` collisions (learned from #60).** A
  resource declared in `Engine/Shaders/Include/Engine.hlsli` is emitted into *every* shader that
  includes it, and with `-fspv-preserve-bindings` (always on) it survives even when unused, so it
  lands in the reflected layout of every pipeline, including the full-screen post passes
  (Fxaa/Sharpen/TemporalResolve), which pair their frag with `Fullscreen.vert` (also includes the
  header) and **park their own cbuffers/textures high in `space1` (bindings 3/4/5/6) to dodge the
  material bindings 0/1/2**. Adding a new binding to the shared header at one of those slots silently
  collides with a post pass's resource of a *different* descriptor type in the same pipeline (a
  validation error, not a compile error: it only shows in smoke). If a binding is used by only one
  shader family (e.g. the shadow comparison sampler is DefaultLit-only), declare it in that shader's
  `.frag`, NOT the shared header, and gate any C++ that binds it on the reflected layout actually
  having that binding (custom-shader materials like Mandelbrot won't).

## Dependencies (vcpkg, x64-windows)

assimp, EnTT, fmt, glew, glfw3, glm, imgui (vulkan+glfw bindings, docking), rttr, spdlog, stb,
Vulkan SDK, vulkan-memory-allocator, gli, volk, spirv-reflect, nlohmann-json. The canonical list
is `PACKAGES` in `Scripts/Generate-Solution.py`; the linkage is in `Snowstorm-Core/CMakeLists.txt`.
Keep those two in sync when adding a dependency.

## Git hygiene

`.gitignore` excludes everything generated: `build/`, `vcpkg/`, `.vs/`, `Engine/cache`, the local
config files (`SnowstormConfig.cfg`, `SnowstormStartup.cfg`), captures and weights (`Dataset/`,
`*.npy`, `*.ssnn`), downloaded tooling (`Tools/rga/`, `Tools/rdts/`), the PT reference cache
(`Scripts/.quality-ref-cache/`), and all solution/project files (`*.sln`, `*.vcxproj*`, `*.cmake`,
`CMakeCache.txt`, `ALL_BUILD.*`, `ZERO_CHECK.*`, `Makefile`). The committed baselines under
`Scripts/{perf,rga,quality}-baseline/*.json` are re-included explicitly, so they survive those rules.
Never commit generated or compiled artifacts. Commit messages in English.

## Think like a real engine

**Always** check how a serious production engine (Unreal, Unity, Godot, modern in-house) does it
*before* proposing or implementing any design. This is a required step, not an optional prompt.
Name the reference model concretely (e.g. "Unity Clear Flags", "Unreal SkyAtmosphere actor", "Godot
WorldEnvironment Background Mode"), state how that engine actually structures the feature, and only
then deliberately decide how far to go for *this* project. If you're unsure how the reference engines
do it, research it (web search / docs) rather than guessing: a vague "engines usually…" is not
acceptable. The point is to anchor every design decision in a proven pattern so today's choice is a
known subset of the real thing, not an accidental invention.

**Lead with the more rigid, long-term-correct option.** When choosing between a quick patch and the
structurally sound design, *propose the sound one first* and recommend it by default (even if it is
more work), and only fall back to the shortcut when there is a concrete reason (time-box, throwaway
code, the right design needs infra that doesn't exist yet). Don't offer the lazy option as the
headline and the good one as an afterthought. Vuk's stated preference: this should feel like a
professional engine, so bias toward the design that a production codebase would actually ship. A
worked example: when per-entity material overrides needed an editor, the rigid choice was to replace
the fixed `mask + one-field-per-property` struct with a *sparse list of named, typed overrides*
(Unity `MaterialPropertyBlock` / Unreal MID) rather than just bolting a picker onto the old shape:
the latter would have had to be ripped out the moment a third override type appeared. The point is not
to build AAA infrastructure (this is a thesis platform), but to make the simplification a *conscious* choice with the real shape
in view, so today's shortcut is a known subset of the right design rather than an accidental dead
end. Call out which parts are intentionally deferred and why, and prefer shortcuts that are a
*smaller version of* the real thing (so they extend later) over ones that would have to be ripped
out. When the "real" way is genuinely cheap, just do it the real way.

**Counterweight, and actively guard against bloat.** "Long-term-correct" is NOT "more layers." Before
adding any new abstraction, base class, wrapper, config knob, or indirection, ask out loud: *does this
earn its keep, or is it speculative?* An abstraction with one caller/subclass, a wrapper that only
saves a few lines, a second way to do something the codebase already does: these are bloat, not
rigor. Prefer the load-bearing primitive over sugar layered on top of it; prefer one clear way to do a
thing over two. On every new feature/implementation, explicitly weigh whether it *adds* surface area
(a concept a future reader must learn, a decision they must make) against what it removes, and say so.
When a proposed piece optimizes the rare case while taxing the common case, that's backwards, so cut it.
The bias toward the production-grade design (above) and the bias against bloat are the same instinct:
build the real shape, but only the parts that are actually load-bearing. When in doubt, leave it out:
re-adding a thin wrapper later is cheap; ripping out an entangled one that grew callers is not. A
worked example: a CRTP `EntitySystem` base was built to wrap the `ParallelForEach` primitive for
single-query systems, then cut. It had one subclass, only fit the one-query case (multi-loop systems
drop back to the primitive anyway), and added a "which base do I derive?" decision to every new
system, all to save ~5 lines. The primitive was the real abstraction; the wrapper was bloat.

Worked example, the **asset pipeline** (the engine's biggest deliberate simplification):

- **Real engines separate source assets from cooked runtime assets.** The file you drop in
  (`.obj`/`.png`/`.fbx`) is the *source*; an *importer* cooks it once into a GPU-ready artifact
  (mesh → packed vertex/index buffers; texture → BC7/ASTC + mips; shader → SPIR-V/DXIL) plus a
  sidecar `.meta` holding a stable GUID + import settings (cf. Unity's `foo.fbx.meta`). Scenes
  reference the **GUID**, never the path, so moving/renaming a file never breaks references.
- An **asset database** maps `GUID → (source, cooked, dependencies, content hash)`; a **file
  watcher** re-cooks only what changed (and its dependents) and hot-reloads it; the runtime
  **streams** cooked assets asynchronously under a memory budget; builds cook only the transitive
  closure of what scenes actually reference (no dead content shipped).
- **What Snowstorm does instead, deliberately:** `Import` just adds a `handle → path` row to a JSON
  registry; there is no cook step (Assimp/dxc/stb run every startup), no `.meta`, no hot-reload, no
  async, no GUID-vs-path indirection (handles are stable but the registry stores raw paths). This is
  acceptable for the thesis. The editor's manual "Import" button mirrors the fact that, in a real
  engine, import is a *deliberate, potentially expensive* step, not a reason the current trivial
  version must stay manual. The honest upgrade path, in order: auto-import on scan → file watcher →
  a cook step with `.meta` sidecars → async streaming. Treat the existing `AssetRegistry` /
  `AssetManagerSingleton` as the seam where that grows.

## Verify before claiming

- This is graphics code: "renders/looks correct" can only be confirmed by **building and running**
  on a machine with a GPU/display. Headless verification is not possible, so say so when you can't run it.
- After non-trivial runtime changes, **build then run `Scripts/smoke-test.py`** (see Smoke test
  above): it catches crashes, hangs, and Vulkan validation/assertion errors that compilation can't.
  A clean smoke run is the minimum bar before claiming a runtime change works.
- Confirm behavior against the actual source/build, not from names. Mark unverified statements as
  assumptions.

### Build verification (learned the hard way)

- **Check the build exit code, not a grepped log.** `cmake --build ... | grep -i error` can miss the
  real failure (MSBuild error formatting varies) and report success on a broken build. Always inspect
  `${PIPESTATUS[0]}` / the actual exit status. A failed compile leaves the **previous** exe in place,
  so the app keeps running stale code and every downstream test is meaningless.
- **Confirm the exe was actually rebuilt** before testing behavior: check the binary's timestamp
  (`ls -l build/Snowstorm-Editor/Debug/Snowstorm-Editor.exe`) is newer than your edit. If a "rebuild"
  didn't update the timestamp, the build failed silently, so fix that first. This is the #1 cause of
  "my change isn't taking effect."
- **A running editor locks the exe.** `LNK1168: cannot open ... for writing` means a previous instance
  is still alive; `taskkill //IM Snowstorm-Editor.exe //F` before rebuilding. A leftover process also
  means you may be looking at an old build.
- Strip all temporary debug probes (logs, on-screen text) before committing, and `git diff` each
  touched file to catch leftovers, since incremental edits during debugging are easy to forget.

### Don't turn the user into your debugger

- Prefer verification you control: headless runs (`SS_SMOKE_FRAMES=N`), startup-time logging, and
  reading source/state. Reserve "please click X and tell me what you see" for genuine final visual
  confirmation, not for diagnosing logic: a manual launch/click/report loop burns the user's time
  and stalls on build/timing artifacts.
- **Keep effort proportional.** Time-box cosmetic/nice-to-have features; if one can't be made to work
  and verified in a couple of clean attempts, drop it rather than rabbit-holing. Commit the larger
  body of working, verified changes promptly instead of leaving it uncommitted while chasing a detail.

### How to debug effectively (don't guess in a loop)

- **Use the instrumentation that already exists BEFORE writing ad-hoc probes.** This engine already has
  rich, always-on timing/state readouts. Check them first instead of scattering `SS_CORE_WARN` probes:
  - The editor's **Performance panel** (`Snowstorm-Editor/Source/System/SceneHierarchySystem.cpp`) shows
    per-phase + per-**system** CPU ms, per-**pass GPU** ms (timestamp scopes), draw/batch/instance/
    triangle counts, and cull stats, smoothed and heat-colored. A "which part of the frame is slow"
    question is usually answered by reading this, not by instrumenting.
  - `SystemManager::GetSystemTimingsMs()` / `GetPhaseTimingsMs()`: per-system/phase CPU time (the same
    data the panel draws), queryable in code.
  - `CommandContext::BeginGpuScope`/`CollectGpuScopes` (`RendererService::GetGpuPassTimes()`): per-pass
    GPU timestamps.
  - The **frame-time watchdog** (`debug.max_frame_ms` CVar / `--max-frame-ms` smoke flag) turns a
    per-frame stall into a headless `[error]` naming the exact frame + duration.
  - The **profiler** (`SS_PROFILE_SCOPE` / `SS_PROFILE_FUNCTION`) for a full cross-thread timeline. Two
    back-ends behind the same macros: **Tracy** (primary, live; connect the Tracy GUI to a running Debug
    build over the network; `TRACY_ENABLE` is on in Debug) and a **headless JSON fallback**
    (`profile.capture_frames` / `profile.capture_path` CVars dump a chrome://tracing / Perfetto file with
    no GUI, for automated/offline trace analysis). Instrumented spots: frame-loop phases, every ECS system,
    and JobSystem worker tasks. One `SS_PROFILE_SCOPE` per lexical scope (it declares a fixed-name RAII
    object, so two in the same block is a redefinition; nest them).
  A whole debugging session was once burned scattering probes to find a load spike that the Performance
  panel would have pinned to `RenderSystem`/shadow-fit in one glance (and the fix, reading the panel, was
  already built). If the existing readouts genuinely don't cover the spot, ADD a permanent, toggleable
  diagnostic there (extend the panel / add a scope / a CVar-gated log) rather than a throwaway probe;
  see "Build the engine to be debuggable" below. Reserve ad-hoc probes for gaps the standing tools can't
  reach, and strip them before committing.
- **Bisect, don't guess.** When behavior contradicts the code, the bug is somewhere between "what I
  believe is true" and "what's observed." Add a probe that splits that gap in half and *prove* which
  side is wrong, rather than changing code speculatively and re-running. One well-placed probe beats
  five hopeful edits.
- **One assumption per probe; isolate the variable.** Each test should answer exactly one yes/no
  question. If a result is paradoxical (e.g. "metadata valid at registration but absent at render"),
  do not theorize further: put *both* readings in a *single build/run* and compare. Contradictions
  across separate runs usually mean the runs differed (stale exe, different selection), not that the
  code is haunted.
- **Verify the harness before the hypothesis.** Before concluding "the code is wrong," confirm the
  test itself is valid: right binary (timestamp), build actually succeeded, the probe code path is
  even reached. Most "impossible" bugs are a broken test, not broken code.
- **Make the probe observable without a human.** Favor startup-time logs and `SS_SMOKE_FRAMES` runs
  whose output you can read directly. An on-screen-only probe that requires a click is a last resort
  and is itself a debuggability smell (see below).
- **When stuck after ~2 failed attempts, change altitude.** Stop poking the same spot: re-read the
  full function (not a snippet), question the premise, or check the layer above/below (build system,
  RTTR registration, ImGui ID/widget state). Repeating a variant of a failed approach is the signal
  to step back, not to try harder.

### Debugging rendering bugs specifically (lead with observation, not code)

A plausible cause is not a proven cause. On a flickering-texture bug the obvious-looking culprits
(missing mipmaps, near-plane z-fighting, depth precision) were all *wrong*: each was "fixed" before
being proven, wasting three rounds. What actually found it: the user's observations + visual probes.

- **Ask "when does it NOT happen?" before reading code.** Which scene only? Which material only?
  Static camera or only in motion? Each answer eliminates a whole class of causes in one sentence.
- **Static vs motion is the big discriminator.** Garbage/flicker *while the camera is static* ⇒ data
  changing per-frame: a race, sync gap, or undefined behaviour (e.g. non-uniform descriptor indexing).
  Shimmer *only under motion* ⇒ aliasing / mip-LOD / depth precision. Pin this first; it splits the
  search space in half immediately.
- **Bisect with temporary shader probes, not theory.** Force a flat color (geometry vs sampling),
  force texture index 0 (slot vs index), output a value as RGB (index magnitude, `SV_InstanceID`),
  force a mip level (`SampleLevel`). Each probe is one yes/no that halves the space; 4 to 5 pin it. This
  is "bisect, don't guess" applied to the GPU. Strip every probe before committing.
- **Don't "fix" before the probe proves the cause.** Prove with one probe, *then* change code.
- **Bindless + instancing red flag:** when one draw renders many objects with *different*
  descriptor-array indices, the index is not dynamically uniform → wrap in `NonUniformResourceIndex()`
  and enable the matching `shader*ArrayNonUniformIndexing` device feature. Silent garbage/flicker
  otherwise; it "works" pre-instancing only because each object was its own draw.
- **A wrong fix that's independently useful can stay.** Misdiagnoses (mipmaps, near plane) were real
  improvements on their own, so keep them; don't revert good changes just because they missed this bug.
- **Color/hue/gamma bug → suspect the color space and pipeline STAGE before the formula.** On a color
  bug, if two fixes fail in the *same category*, stop tweaking the math and ask: wrong color space
  (linear vs display/sRGB, HDR vs tonemap-compressed, RGB vs signed-chroma like YCoCg) or wrong pipeline
  stage (before vs after tonemap)? Several correct-looking diagnoses that all trace to one structural
  fact = that fact is the bug. Worked example (#44): an in-resolve TAA sharpen corrupted edge colors
  through five formula rewrites (signed-chroma clamp, then dark-biased low-pass, then pre-tonemap hue
  shift), all one root cause: **sharpening in linear HDR before the per-channel ACES tonemap**, which curves
  R/G/B differently and turns any overshoot into a hue shift. This is the "change altitude after ~2
  failed attempts" rule applied to color: interrogate placement, not parameters.

**Pipeline-stage invariant (learned from #44).** The **temporal resolve runs in linear HDR and does
accumulation only**. Perceptual / display-space operations (**sharpening, CAS, contrast, any
per-channel curve**) belong **after** tonemap (a post-tonemap pass, like FXAA on the LDR present
target), NEVER inside the resolve or any pre-tonemap linear-HDR stage: an overshoot that's a neutral
brightness change in linear becomes a **hue shift** once ACES curves each channel. One pass = one
responsibility: don't bolt a display-space effect onto a linear-HDR pass.

### Build the engine to be debuggable

The deeper fix for "I couldn't verify without the user" is to make state inspectable in code:

- **Expose state to headless inspection.** If you can only confirm a feature by looking at the screen,
  add a non-visual path to read the same truth: a startup/CVar-gated dump, a query function, or a log.
  The inspector's reflection (RTTR) and the `smoke.frames` hook already make a lot of state reachable
  without a GPU, so prefer wiring new state through those.
- **Prefer pure, testable cores.** Logic that maps data→data (name formatting, layout math, value
  conversions, asset-handle resolution) should live in free functions that a Catch2 test or a headless
  run can exercise directly, not be entangled in an ImGui draw call that only runs on a click.
- **Fail loud, not silent.** Silent fallbacks (a missing asset resolving to null, an unread metadata
  key, a default value) hide bugs and force interactive spelunking. Log once at `[error]`/`[warn]`
  when an expectation is violated, the way `ResolveAssetName` / the unresolved-handle path do.
- **Name things for diagnosis.** Vulkan objects via `SetVulkanObjectName`, ImGui widgets with stable
  unique IDs, components with reflected type names, so logs, validation, and RenderDoc say
  `Swapchain[0]` / `DIRECTIONAL LIGHT`, not an opaque handle.
- **Treat "I had to add a temporary on-screen probe" as a missing feature.** It usually means that
  state should be permanently visible (a debug overlay / stats panel / CVar dump). Consider promoting
  the probe into a real, toggleable diagnostic instead of deleting it.
