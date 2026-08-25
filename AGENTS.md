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
frame budget and writes a JSON (`PerfBench.hpp` builds it; `Application::Run` drives it past a detected
steady state, which also covers the 1-frame timestamp lag; see "Warmup is detected, not assumed" below).
The script parses each JSON, prints a per-pass
table, and **diffs against a committed baseline** in `Scripts/perf-baseline/`, failing (exit 1) if any
pass regresses beyond **both** `--threshold` (default 15%) and `--abs-threshold` (default 0.10 ms).
Both apply because a percentage alone is meaningless on a sub-ms pass: back-to-back runs of the same
binary move `TemporalResolve` by ~0.08 ms, which reads as +50% on a 0.16 ms pass. The absolute floor
keeps those passes gated against a real regression rather than excluding them outright.

```
py Scripts/perf-bench.py                    # run the matrix, diff vs baseline, PASS/FAIL
py Scripts/perf-bench.py --update-baseline  # capture current results as the new baseline
py Scripts/perf-bench.py --only +gi         # one config (rt-off | shadows | +ao | +refl | +gi | ssgi | shadows-stoch)
py Scripts/perf-bench.py --frames 300       # more frames = less noise WITHIN a run
py Scripts/perf-bench.py --repeat 5         # more independent runs = less noise BETWEEN runs (default 3)
py Scripts/perf-bench.py --compare-exe <ref-exe>   # interleaved A/B vs another build (measure a CHANGE)
py Scripts/perf-bench.py --canary-pass Editor      # normalise out a global clock shift
py Scripts/perf-bench.py --gpu 5070         # pin the adapter on a multi-GPU box
```

The config matrix (`rt-off → shadows → +ao → +refl → +gi`) enables one RT effect at a time, so the
**Forward-pass ms delta between adjacent configs is that effect's cost**: the RT effects are inline in
the Forward pass, so this A/B *is* the per-effect timing (there's no separate GPU scope per effect, by
design). Two trailing configs sit outside that ladder, each repeating one rung with a different
producer for the same effect: `ssgi` repeats `+gi` with the screen-space GI producer (so it diffs
against `+refl`), and `shadows-stoch` repeats `shadows` with the stochastic aggregate pass
(MegaLights-lite) instead of one inline ray per light (so it diffs against `rt-off`). Read the
shadow pair as one point on a curve, not a verdict: inline cost grows per light while stochastic is
constant, so the ranking flips with light count and a fixed scene cannot show that.

**The stochastic producer is EXPERIMENTAL and off by default; inline RT is the supported path.** It
is more temporally stable on the gate (tFLIP 0.0131 against inline's 0.0154) and constant in light
count, but it shows artifacts under camera motion where two point lights overlap, consistent with
per-pixel light selection differing between neighbouring pixels, and it leans on the shadow a-trous
to hide them (#170). Neither the gate nor perf-bench found this, because the route never renders the
lit side aisles (#169). Treat its numbers as characterising a prototype, not a shipping technique. It also flips on
`render.shadows.specular.demodulated`, which the rung leaves at its shipping default (on) and which
adds a second denoise chain the inline path has no analogue for. At Sponza's light count, in ms over
`rt-off`: 9070 XT inline 2.388, stochastic 2.757 with that chain and 1.677 without; 5070 inline
1.428, stochastic 3.510 and 2.144. So the shipping configs rank inline first on both adapters, while
the ray-tracing work alone ranks stochastic first on AMD. The `ShadowSpec*` rows in the per-pass
table are that chain, priced separately. Sub-0.05 ms passes are ignored (timestamp noise).

**Warmup is detected, not assumed.** Sampling begins once the rolling 30-frame GPU-time window's
peak-to-peak spread drops below `perf.bench.warmup.epsilon` (default 5%) of its mean, capped by
`perf.bench.warmup.maxframes` (600, after which the run proceeds and logs that it is NOT from a steady
state). The old fixed 15-frame warmup was ~0.25s and measured on Sponza takes **53 frames** to actually
settle, so every run before this averaged part of the GPU clock ramp, with the result depending on how
warm the machine already was and no way to tell from the JSON.

**The dominant noise source is run-level, not frame-level.** Three identical back-to-back runs on an RX
9060 XT spread **8-12% on every pass**, while each pass's *minimum* moved only ~3%: the GPU still reaches
peak briefly in every run but spends progressively more frames throttled, which is a DVFS/thermal
signature rather than workload variance (every pass moving by the same factor is a clock change, not a
code change). More `--frames` cannot average that out because it is drift BETWEEN launches, so the script
runs each config `--repeat` times (default 3) and takes the **median**, which rejects a single throttled
outlier as a mean cannot. Each pass carries a quartile **interval** (`q1Ms`/`q3Ms`) alongside the median, and the gate compares
INTERVALS rather than a point delta against a fixed percentage: disjoint with the current run higher is a
regression, overlapping means the runs do not separate whatever the point delta says, and that reads
**INCONCLUSIVE** rather than PASS. A gate must not rule on a difference below its own measurement error.
Baselines predating the interval fields (or `--repeat 1`, which cannot produce one) fall back to the
threshold plus a range check. **Check what else is using the GPU before believing any number.** A remote-desktop session
(Parsec/RDP/Steam Link) hardware-encodes the framebuffer on the same adapter, and its load tracks screen
content and network conditions, which reproduces exactly this signature. So does a browser or Discord with
GPU acceleration on. Benchmark from the console with the streamer stopped, or accept that only a paired
A/B is trustworthy.

**Interval separation is not a magnitude test, so a regression must also clear `--abs-threshold`.** With a
`--repeat 5` baseline the intervals get tight enough that any reproducible sub-1% difference separates
cleanly: without the floor, a baseline re-gated minutes later against the binary that captured it fails on
both adapters, on differences as small as 0.000 ms. Separation below the floor still prints, as `separated
but under floor`, so a small real shift stays visible without failing the run.

**`rt-off` on the 9070 XT is noise-dominated and its absolute numbers are not portable.** It is the only
config light enough (~2 ms/frame) to leave the GPU idle between frames in a Debug build, so the adapter
downclocks and every pass inflates together. Measured: 8.3% run spread at `--repeat 5` against 0.2-2.2% for
every other config, and the same config on the 5070 spreads 2.8%. A ~20% swing there is the config, not the
change, and an interleaved `--compare-exe` A/B is the only way to read it; the golden-baseline path cannot.

**To measure a CHANGE, use the paired A/B, not the golden baseline.** `--compare-exe <ref-exe>` runs two
builds interleaved (A,B,A,B,...) in one session and reports the median per-pair delta with the pair-to-pair
spread. A stored baseline cannot be corrected for drift: it carries whatever clock, thermal and contention
state existed when it was captured. Alternating inside one session puts both arms under the same
conditions, so common-mode noise cancels in the difference. A median delta smaller than the spread means
the pairs disagree and prints `inconclusive`. This needs no baseline, so it also works on an adapter that
has none. The golden-file path answers a different question ("did this drift from the committed numbers")
and stays the right tool for regression gating.

`--canary-pass <name>` is the golden path's remaining correction: it scales the run by how much a pass
the change CANNOT affect moved against the baseline, cancelling a global clock shift that path has no
other way to see. It has no default on purpose, since naming a pass the change does affect silently
rescales away the result, and only the caller knows which is safe. It corrects a multiplicative shift
hitting everything alike, not contention landing unevenly.

Removing the noise at its source still beats averaging over it: a **stable power state** (AMD via the
Radeon Developer Tool Suite, NVIDIA via `nvidia-smi --lock-gpu-clocks`) is the real fix where the hardware
and tooling allow it. Like
smoke, it needs a **real GPU** (Vulkan timestamps) so it's a **local** gate, not CI; on a device
without timestamp support the JSON sets `timestampsSupported:false` and the run counts as a SKIP
(exit **2**), never a pass and never a false FAIL.

**Baselines are keyed by adapter**: `Scripts/perf-baseline/<device-slug>/<config>.json`, where the slug
comes from the device name the run reports. GPU differences make ms non-comparable, so a run only ever
diffs against the set captured on the adapter it is running on, and a box with two cards keeps two
independent sets (the repo holds `amd-radeon-rx-9070-xt`, `nvidia-geforce-rtx-5070`, and an
`amd-radeon-rx-7900-xtx` set from another machine, the last predating the resolution/pose stamping
below and so of unknown viewpoint until that box re-captures). Cross-vendor comparison is then a deliberate read
across two directories rather than an accidental mixture inside one set, which would turn every
cross-config delta into effect-cost plus hardware difference. Missing set = the gate prints the raw
numbers and exits **2** (SKIP), which is not a pass: it compared nothing, so it cannot claim one.
On a multi-GPU machine `SS_CONFIG_IGNORE` also discards the persisted
`render.gpu` pick, so selection falls back to auto and the adapter is whatever the driver enumerates
first; `--gpu` pins it, taking a short all-digits value as a candidate **index** and anything else
(including a model number like `9070`) as a case-insensitive name substring. Re-baseline deliberately
(with a commit) when a change *intends* to shift perf, never to paper over an unexplained regression.

**A baseline is also keyed by resolution and viewpoint**, and both are now recorded rather than
assumed. The Editor renders at the window size, which no CVar pins, so every JSON stamps `width`,
`height`, and the 6-value `camera` pose; a mismatch against the baseline is a SKIP (exit **2**), not
a diff. Without that, a different monitor or window size moves *every* pass by roughly the same
factor and reads as a global regression. The viewpoint is pinned by the script rather than gated:
`BENCH_CAMERA` in `perf-bench.py` is fed to `camera.override`, so the pose lives in the repo instead
of in `<scene>.world.editor`, which is per-machine working state the editor rewrites on every save.
Changing `BENCH_CAMERA` invalidates every baseline, so re-capture all adapters in the same commit.
A baseline predating these fields carries none of them and is compared without the check.

**Nondeterministic GPU numbers across runs point at the shader cache first.** Clear
`Engine/cache/shaders/*.spv` and re-run before trusting any before/after comparison; a stale cache
means the run measured a mixture of old and new shaders. If the numbers still move after a clean
cache, that is a real race and a genuine finding.

## Shader occupancy gate (RGA, static)

`Scripts/rga-occupancy.py` is the static analogue of perf-bench: a golden-file gate on shader
register/LDS pressure and spills, the determinants of GPU occupancy. It needs no GPU run. It feeds
every compiled SPIR-V module in `Engine/cache/shaders/` through the Radeon GPU Analyzer offline
compiler for a target ASIC (default `gfx1100`, the RX 7900 XTX; `--asic gfx1200` for RDNA4), parses the per-shader stats CSV
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
derives waves/SIMD from the VGPR count, per architecture family, verified against AMD's docs. RDNA3
(`gfx11`) and RDNA4 (`gfx12`) desktop parts share a 1536-VGPR (192 KB) file and 16 wave slots, so both
reach full occupancy at <=96 VGPRs, but allocation granularity differs (16 vs 24), which moves where
the wave count steps: they disagree at 56 of the 256 possible VGPR counts, so neither model stands in
for the other. An unmodelled architecture yields no occupancy figure rather than a wrong one, which
disables the primary gate, so add a model before targeting a new family. It fails (exit 1) when
occupancy drops (fewer waves), a spill appears (0 to >0, hard fail), or LDS/ISA rises beyond
`--threshold` (default 10%). Raw VGPR% is intentionally not gated -- a VGPR rise that doesn't cross a
wave boundary costs nothing. The occupancy is *theoretical* and VGPR-only (LDS occupancy needs the
workgroup size RGA offline reports as 0); measure achieved occupancy with RGP. Baselines are committed
for `gfx1100` (RX 7900 XTX) and `gfx1200` (RX 9060 XT); on both, only `GIDenoise.comp` and
`DefaultLit.frag` are occupancy-limited (8/16 and 10/16 on RDNA4), and the other 40 shaders hit 16/16
with zero spills.

**RGA is AMD-only**, so this gate cannot cover NVIDIA: its target list is `gfx11xx`/`gfx12xx` and
nothing else. The Vulkan-native equivalent is `VK_KHR_pipeline_executable_properties`
(`vkGetPipelineExecutableStatisticsKHR` reports register count and spills once pipelines are created
with `VK_PIPELINE_CREATE_2_CAPTURE_STATISTICS_BIT_KHR`), which both vendors' drivers implement. That
is a *runtime* query needing the real device and driver, not a static offline pass, so it would be a
separate GPU-gated harness rather than an extension of this one, trading CI coverage for numbers that
reflect the shipping compiler.
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
- **THIS gate needs a static camera.** Leave `camera.path` out of any run whose numbers are compared
  against another run of *this* script: it moves the camera while the gate is waiting for a converged
  fixed point that a moving image never reaches. The fixed viewpoints (`camera.override`,
  `SS_CAMERA_OVERRIDE`) exist for that reason and pin an arbitrary interior pose headlessly. Both
  hosts honour it: the Runtime applies it when it builds the authored camera, the Editor when it opens
  a scene, outranking the `<scene>.world.editor` sidecar. Measuring UNDER motion is a different gate
  with a different protocol, `quality-motion.py` below; it is not this one with the path switched on.

Every run sets `SS_CONFIG_IGNORE=1`, so a capture is code defaults plus the technique's overrides and
never this machine's persisted `SnowstormConfig.cfg`. Note that only `all-rt` overrides
`render.shadows.mode`: every other technique renders with the default shadow map, so shadowing is a
constant in the matrix rather than something the gate measures.

## Image-quality gate UNDER MOTION (run after any change to TAA, a denoiser, or reprojection)

`Scripts/quality-motion.py` is the temporal counterpart to `quality-bench.py`. A static viewpoint is
the wrong test for anything temporal: TAA, the SVGF-style denoisers and the stochastic shadow pass
all exist to exploit motion, and their characteristic failures (ghosting, disocclusion trails,
history rejection, boiling) appear only while the camera moves. This gate flies a committed camera
route and measures the frames rendered along it.

```
py Scripts/quality-motion.py                    # all techniques, diff vs baseline
py Scripts/quality-motion.py --update-baseline  # capture current metrics as the new baseline
py Scripts/quality-motion.py --only rtgi        # one technique
py Scripts/quality-motion.py --probes strafe    # one probe point
py Scripts/quality-motion.py --fresh-ref        # ignore cached PT references
```

**Protocol** (BMFR's, Koskela et al. TOG 2019: an animated sequence with a converged per-frame
reference, metrics averaged over it). One headless Runtime run per technique flies `camera.path` with
`quality.capture.at_path_frames` set, writing those route frames plus a `_poses.json` manifest of the
pose each was taken at. Ground truth for each captured frame is then a **separate path trace of the
world replayed to that frame and held there** (`sim.freeze_frame`), which is forced rather than
chosen: the path tracer resets accumulation on any view-projection change, so a moving path trace is
a one-sample noise image that never converges.

Freezing has to stop the whole world, not just pin the camera. `camera.override` holds the viewpoint
while animated props keep spinning through a 1200-frame accumulation, so the reference converges to a
smear of every angle they passed rather than the state the captured frame had. For the same reason
the reference cache is keyed on the ROUTE FRAME, never the pose: the route parks at its end, so
frames 900 and 901 share a pose while the props sit at different angles.

**The scene is `Sponza-Motion.world`**, Sponza plus four rotating props at eye height between the
route and the colonnade. The static gates keep plain `Sponza.world` deliberately: animating props
there would invalidate every committed quality-bench and perf baseline. A frame cap below the freeze
point silently breaks the control, since reaching route frame N means rendering N frames first.

**Frames come in adjacent pairs** (N, N+1), because averaging per-frame FLIP over a sequence provably
cannot separate stable distortion from flicker: two techniques with identical mean spatial error rank
the same whether one is steady and the other strobes. Four probes name route phases rather than
spacing evenly, since each breaks reconstruction differently: `dolly` (forward translation, mostly
history length), `strafe` (lateral motion past the colonnade, the canonical disocclusion generator),
`reversal` (the U-turn; rapid direction change is a documented pathological case), and `static` (past
the end of the open route, where it parks, so convergence once motion stops is measured too).

**Metrics.** Per-frame FLIP/PSNR/SSIM against that frame's reference, plus **tFLIP**, gated. tFLIP is
tLP's construction (Chu et al., TecoGAN, TOG 2020) with FLIP substituted for LPIPS: it compares how
much the technique changed between consecutive frames against how much the **reference** changed over
the same interval. The subtraction is the point, and it is measurable: on Sponza's dolly probe the
reference itself moves by FLIP 0.0932 between consecutive frames, so a naive frame-difference metric
reports 0.1151 as instability when the technique's actual excess is 0.0219, over 4x too high, and it
would rank a blurrier laggier result as more "stable". Report it as tFLIP, never as tLP: substituting
FLIP is a deliberate deviation that keeps torch out of the gate.

**ColorVideoVDP** (Mantiuk et al., TOG 2024) is reported when importable, never gated. It is the one
metric here that models temporal contrast sensitivity directly rather than inferring it, so it is the
one worth citing, but it needs torch. It is **not on PyPI**:

```
pip install git+https://github.com/gfxdisp/ColorVideoVDP.git
```

It is evaluated **per probe pair**, never over all captured frames at once. The probes sit far apart
on the route, so concatenating them hands the metric three enormous scene cuts which it reads as real
temporal content: measured, that inflates `all-rt` by 0.627 JOD, and the paper puts 1 JOD at roughly a
75% population preference. Two frames is thin temporal context, and thin beats fabricated.

**The two temporal metrics disagree, usefully.** On the 9070 XT baseline `rtgi` has the best tFLIP
(0.0078 vs `all-rt`'s 0.0126) while `all-rt` has the best JOD (5.090 vs 4.099). No contradiction:
tFLIP isolates temporal excess alone, ColorVideoVDP is spatio-temporal and `all-rt` is far more
accurate spatially. Read tFLIP for stability and JOD for overall perceived quality.

**The per-frame FLIP/PSNR/SSIM here are a SPATIAL gate measured under motion, not a temporal one.**
Measured across the technique matrix, this gate's mean FLIP correlates **+0.995** with quality-bench's
static FLIP: it is almost entirely re-measuring spatial accuracy. That is inherent to the reference,
not a bug in it. A converged per-frame path trace is a sequence of independent stills with no temporal
structure, so error against it has no temporal component to carry. The construction itself is the
field standard (BMFR, NoiseBase and Arm NSS all use per-frame converged references for animated
sequences, and freezing the world at state N to accumulate is identical to rendering frame N at high
spp when no shutter is modelled). What the canonical papers do NOT do is call it a temporal result:
SVGF and A-SVGF both report quality against a reference AND, separately, a reference-free temporal
number measured with a STATIC camera. k-DOP Clipping (Ikkala et al., SIGGRAPH Asia 2024) states the
conflation outright, that a supersampled reference "would introduce a constant error by also comparing
general TAA quality against supersampling".

So read the three families separately: FLIP/PSNR/SSIM for accuracy, **tFLIP** for stability,
**motionPenalty** for lag. Only the last two carry information the static gate does not.

**Naive frame-difference temporal metrics reward blur, and this is verified twice over.** SVGF says it
of its own results: EAW has the lowest temporal error while losing significant detail. TecoGAN
measures it, with ground truth scoring WORSE on T-diff (5.184) than bicubic upsampling (3.152), and
calls low T-diff from smooth output "an easy, but undesirable avenue for achieving coherency". That is
exactly why tFLIP subtracts the reference's own inter-frame change, and TecoGAN explicitly licenses
substituting a different perceptual metric into that construction (they swapped LPIPS for PieAPP and
got near-identical results), which is what tFLIP is.

**Ghosting has no published metric.** A-SVGF's headline contribution is "a significant reduction of
lag and ghosting" and the paper contains no lag or ghosting number anywhere; AMD, NVIDIA and Intel all
describe ghosting qualitatively. motionPenalty is therefore closer to a ghosting number than anything
shipped, which is a reason to report it carefully rather than confidently.

**Coverage is the weakest part of this gate, by a published standard.** ITU-T P.910 asks for at least
four source sequences spanning the SI/TI plane plus variety beyond it. This gate is one scene, one
route (two since #169) and four adjacent frame pairs, against SVGF's 5 scenes, A-SVGF's 4, BMFR's
7x60 frames and CG-VQD's 15 scenes. Finding an artifact off-route is the predicted consequence, not
bad luck.

**Do not rank the probes by comparing their scores to each other.** Each probe looks at different
content, so an absolute FLIP or JOD difference between two probes is mostly a difference in what is
on screen, not in how hard the motion is. JOD ranks `strafe` worst on every technique, and that is
NOT evidence that strafing is the hardest motion. Attributing error to motion needs a static control
at the identical pose, and measured that way (moving capture minus a static capture at the same
recorded pose, `all-rt`) the order is different: dolly +0.0231, strafe +0.0384, **reversal +0.0439**.
The `raster` row makes the trap explicit, since its `reversal` FLIP is the BEST of its four probes
while its motion penalty is the worst.

Velocity REVERSAL is therefore the costliest motion here, and it is not explained by speed or by
angular rate: arc-length reparameterisation holds translation at 0.0333-0.0340 units/frame across
every moving probe, and `strafe` carries 24x the yaw rate of `dolly` while costing less than the
reversal. What is special about the U-turn is that the velocity vector changes sign, which is the
documented pathological case for history rejection (FSR2 tracks high-velocity ghosting as its own
bug class). The `static` probe is the control that makes all of this readable: its moving-minus-static
delta is 0.0000 on every technique, so a parked capture and a static capture agree exactly.

Baselines are keyed by adapter (`Scripts/quality-motion-baseline/<device-slug>/<technique>.json`) for
the same reason the static ones are: the reference is a path trace on the local card. Exit 0 within
threshold, 1 on a regression, **2** when nothing was compared. Needs a real GPU, so it is a local
gate, not CI.

**Every technique must fly the identical route**, or its frames and the cached references are
different viewpoints. The script re-checks each technique's manifest against the first one's and
hard-fails on any divergence rather than trusting it, because that determinism is the single
assumption the whole protocol rests on.

### Tuning against motion, and why the objective matters

`quality-tune.py --motion` flies the route and scores the probe frames instead of the static
viewpoints, searching `MOTION_PARAM_SPACE`: the denoiser and temporal-blend knobs the static
objective is measured blind to. `--objective jod` (default) minimises negated ColorVideoVDP;
`--objective flip` is kept for comparison and is measurably the wrong choice.

**FLIP is not a usable objective for a denoiser.** Tuning `all-rt`'s temporal knobs against motion
FLIP boundary-clamped four of six knobs, pinning both `ao.denoise.iterations` and
`reflections.denoise.iterations` at ZERO. Moving the camera does not fix that: FLIP rewards removing
blur either way. This is the repo's own DLSS-selection lesson one metric up, where PSNR favours the
blurry image so LPIPS decides; here FLIP favours the sharp noisy one, so a perceptual spatio-temporal
metric decides. Never optimise **motion penalty** either: it is moving-minus-static, so an optimiser
can drive it to zero by degrading the static case.

**A scene without object motion cannot answer the denoiser question at all.** Measured on GI a-trous
iterations by JOD, on static geometry versus with animated occluders:

| iterations | Sponza.world | Sponza-Motion.world |
|---|---|---|
| 0 | 5.0809 | 4.2897 |
| 3 (default) | **5.0905** | 4.3072 |
| 5 | 5.0742 (turns over) | **4.3181** (ceiling) |

On static content the curve peaks at the shipped default and degrades past it; with object
disocclusion it climbs. So the filter earns its keep precisely where the old scene had nothing to
measure, and a tuning run on static geometry would have deleted it.

Swept the full 0..5 range on the animated scene, the curve **plateaus at 4** (4 and 5 differ by
0.0002 JOD), so it does not keep climbing and the ceiling was never the binding constraint. Going
3 -> 5 buys 0.0076 JOD for 0.221 ms of extra dispatches, +2.2% frame time against a metric where 1
JOD is roughly a 75% population preference. **The default stays 3.**

`kMaxDenoiseIterations` lives in `EngineCVars.hpp` because `GIDenoisePass`'s per-frame
descriptor/uniform pool must have one slot per pass. They were separately-written 5s; raising only
the CVar clamp made iterations 6+ assert at runtime rather than fail to build.

### Why AO and reflections behave differently from GI

They clamp to 0 for unrelated reasons, and only one of them is a defect.

There is only **one** a-trous shader. `GIDenoise.comp.hlsl` filters all three signals through the
shared `Denoiser`, guided by the **receiver's** geometric normal and depth from G-buffer attachment 0.

**AO is a non-event.** Its only differentiator is `HitDistPhi`, fed by `render.ao.denoise.hitdist`,
which defaults to 0, so `wH` is identically 1 and AO's filter is bit-identical to GI's. The signal is
a distance-weighted visibility integral (smooth by construction) that already passes through temporal
accumulation at 0.97 and a depth/normal bilateral upsample, so the a-trous has nothing left to
remove. Measured: 0 vs 5 iterations moves the image only 59 dB (GI moves it 42-49 dB) and leaves mean
FLIP unchanged. The 0..5 JOD span is 0.0054, which is noise, so the optimiser is picking between
indistinguishable options.

**Reflections is a real defect.** The span is monotonic and 10x AO's, and inside the filter's own
footprint (the top 5% of pixels it changes) error against the reference *rises* from 23.327 at 0
iterations to 24.103 at 5. The cause is structural: reflected radiance is view-dependent, so on a
flat reflective surface the receiver's normal and depth are constant, `wN` and `wD` are both 1, and
the kernel is wide open exactly where reflected detail lives. The code states the false premise
outright, that "reflection edges are receiver-surface edges".

**Two fixes were tried and both measured flat.** `render.reflections.denoise.variance` swept 0..8 is
non-monotonic with a 0.0013 JOD span, so the luminance term is not the lever. `Reflection.comp` does
trace hit distance into `.a`, and AO is handed a real hit guide for precisely this reason while
reflections passed the G-buffer as an "(ignored) hit guide", so `render.reflections.denoise.hitdist`
now wires the real one: swept 0..4 it moves 0.0002 JOD, which is noise. The obvious explanation, that
1 ray/pixel makes the guide too noisy to steer anything, does not survive either: at
`render.reflections.rays` 8 the span is still 0.0002. Hit-distance guidance is not the lever here.

**What is measurable is that the pass should probably not run.** On the animated scene, `all-rt` with
`render.reflections.denoise.iterations` 0 versus the default 3: FLIP 0.1742 vs 0.1746, PSNR 21.7513
vs 21.7047, SSIM 0.6473 vs 0.6419, JOD 4.7933 vs 4.7728, with tFLIP and motion penalty unchanged. All
four spatial metrics improve, SSIM included, which is what separates this from metric gaming: the
blur-removal signature is FLIP and PSNR improving while SSIM DROPS. It is also 1.115 ms, 13.5% of the
frame at the `+refl` rung, since the reflection a-trous is full-res and unlike GI's has no upsample
after it. The default is unchanged pending eyes-on, because every number here is whole-image and a
metric can underweight noise that reads badly in motion.

A roughness-driven kernel (the NRD ReBLUR model, using the roughness sitting unread in G-buffer `.z`)
remains the structurally right fix, but note the bar it now has to clear: filter-off is already
ahead, so a smarter kernel has to beat OFF, not beat 3.

### The benchmark camera route

`camera.path.file` names a JSON route (`Projects/Sandbox/assets/camera-paths/sponza-bench.json`)
flown as a Catmull-Rom spline with arc-length reparameterisation, so world speed and therefore
motion-vector magnitude stay uniform regardless of waypoint spacing. Empty falls back to the legacy
circular orbit, which suits an open scene and nothing else: in Sponza that orbit is outside the clear
nave for 86.7% of its loop and outside the atrium walls entirely for 58.1%, at a height that
intersects the upper arcade. Its closest approach to geometry is 0.239 m, inside the west arcade; the
committed route's worst is 0.967 m.

A waypoint carries a position **and a look-at target**, and both are splined, so orientation comes out
C1 from the same curve. Interpolating orientations directly instead is C0: angular velocity jumps at
every waypoint, putting a discontinuity into the motion vectors, in a route whose whole purpose is to
exercise motion-vector-driven reprojection.

An **open** route parks at its final waypoint once distance exceeds arc length. That is load-bearing,
not incidental: with a constant world speed there is no other way to express a hold, and it is what
gives the `static` probe something to measure.

`CameraRouteTests` samples the **curve**, not the waypoints, against the measured interior of the
nave. Catmull-Rom overshoots outside its control points at a direction reversal and this route has
one, so checking only the authored points would not prove containment. Changing the route invalidates
every motion baseline, exactly like `BENCH_CAMERA` does for perf: re-capture in the same commit.

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
every CVar with its value, type, env name, and description. `EngineCVars.cpp` declares ~126 of them
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
