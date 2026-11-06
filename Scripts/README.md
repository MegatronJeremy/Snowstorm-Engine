# Scripts: tests, gates and measurement tools

The local and CI gates for the engine, and the measurement tools behind every number committed in the repo. Gates exit 0 on pass and 1 on fail. The ones that compare against a baseline exit 2 when nothing was compared, which is never a pass.

## Smoke test (run after non-trivial changes)

`Scripts/smoke-test.py` boots each executable headlessly and checks it doesn't crash or log
errors. The example game is one of those executables, which is what keeps it from rotting: a target in
`TARGETS` may name a scene it needs, and `Pong` boots `Pong.world` so it runs actual gameplay rather
than whatever the startup project points at. It launches every app with `SS_SMOKE_FRAMES` set (the engine then runs that many frames
and exits cleanly), captures stdout/stderr, enforces a per-app wall-clock timeout (a hang/deadlock
becomes a failure instead of blocking), checks the exit code, and scans the log for error markers
(`[error]`/`[critical]`, Vulkan validation, assertion text). Exit 0 = all pass.

```
py Scripts/smoke-test.py                 # 120 frames, 180s timeout/app (Editor 480s: it pays the cold cook), Debug build
py Scripts/smoke-test.py --frames 300    # longer soak
py Scripts/smoke-test.py --only Editor   # single target (Editor | Runtime | Pong | Reflections | AlphaBlend | Sprites)
py Scripts/smoke-test.py --only Runtime --project Projects/Pong/Pong.ssproj   # boot a non-Sandbox project the way Play Standalone does
py Scripts/smoke-test.py --warnings-fail # treat [warning] lines as failures too
py Scripts/smoke-test.py --from-exe-dir  # working directory = the exe's folder, as a double-click launches it
py Scripts/smoke-test.py --staged        # run the PACKAGE in build/stage/<config>, not the build tree
py Scripts/smoke-test.py --strict        # enable deeper Vulkan validation (see below)
```

`--exe <path> [--cwd <dir>]` smoke-tests one executable built outside this repository (a game consuming the engine as a submodule, such as Snowstorm-Doom): same environment, settle rule, timeout, `--frames`, `--strict`, `--headless`, `--scene`/`--project` and log scan as a built-in target, named after the exe's stem. A relative `--exe` and `--cwd` resolve against the directory the script is run from, which is also the default working directory, since a game resolves its own `Projects/...` against its root. Validation layers still come from this repository's vcpkg tree (`--triplet`). It excludes `--only` and `--staged`. `Scripts/test_smoke_test_exe.py` covers the resolution and flag rules GPU-free; a real run of an external game is unverified.

`--scene` and `--project` skip `Pong`, which boots its own project: forcing a Sandbox scene on it resolves the scene's handles against Pong's registry and fails with "mesh handle not found".

`--project <.ssproj>` passes `--startup.project=` to each target it runs, the hand-off *Play Standalone* makes. Use it with `--only Runtime` and a project other than Sandbox; the settle rule below then fails on that project's assets that never resolve. `StandalonePlayTests` pin the full argument list GPU-free. The settle check's behaviour on a project's unresolved assets is unverified on a GPU.

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

`Reflections` and `AlphaBlend` are the Runtime booted on the testbed scenes of the same names (`AlphaBlend` with `SS_RENDER_TRANSPARENT=1`), so a scene or flag that stops loading fails the gate (#282). A target whose scene file is missing fails before anything launches; `--scene` overrides every target's scene. Neither has run on a GPU yet.

**Headless mode (`SS_HEADLESS=1`, or `--headless` on `smoke-test.py` and `perf-bench.py`) needs no unlocked desktop.** The `headless` CVar creates the GLFW window hidden (ImGui's GLFW backend and input still get a window handle), creates no `VkSurfaceKHR` or swapchain, and makes `VulkanContext` own four offscreen BGRA8 images in the swapchain's place. `BeginFrame` signals the image-available semaphore with an empty submit instead of acquiring, and the end of the frame consumes the render-finished semaphore with an empty wait-only submit instead of presenting. Device and queues, the frame graph, asset streaming and the settle check run as before. Both scripts print `Mode:` as `HEADLESS` or `windowed`. Headless perf numbers skip present, so capture committed perf baselines windowed.

**A hung GPU must not take the machine with it.** The harness kills the process tree when the app prints nothing for `--stall-timeout` seconds (default 60; 0 = off) as well as at the wall-clock `--timeout`, and `VK_ERROR_DEVICE_LOST` / "device lost" anywhere in the log fails the run. On AMD a hung submission can end in a Windows bugcheck (0x133) instead of a device reset, so the earliest sign has to stop the run. `Scripts/test_smoke_test_watchdog.py` covers the watchdog and the marker GPU-free.

**Cached SPIR-V is validated before use.** `Engine/cache/shaders/<stem>_<key>.spv` has a sidecar `.spv.key` (key, size, content hash). `ValidateCachedSpirv` (`Render/SpirvBlobValidation.hpp`, `SpirvBlobValidationTests`) rejects a blob with a bad magic, a version outside 1.0-1.5, a size or hash that differs from its stamp, a stamp for another key, or no stamp; `VulkanShader` then deletes it and recompiles with dxc. A cache from before the sidecar existed is recompiled once, so a build shipping a warm cache without dxc must ship the sidecars too. A rejected blob is deleted even when dxc is missing, so that call then fails. Deletion happens only under the per-path compile lock; the unlocked pre-check never removes. The compiler publishes the stamp before renaming the blob into place, so a blob is never visible without its stamp. Unverified on a GPU.

**A run passes only once assets settle.** After the frame budget the app keeps running until
`AssetManagerSingleton::PendingLoadCount() == 0` and no bindless slot is still the placeholder
(`UnsettledReason`, reached through `Layer::SmokeUnsettled`), until `smoke.settle.seconds` have passed since process start
(the harness sets it to `--timeout` minus 10); otherwise it logs `[error]` and fails. A stuck cook
used to pass because only the frame count was checked. `--cold --scene <Sponza.world>` covers the
cook path, and needs a generous `--timeout`.

**Smoke and perf-bench do not cover the unit tests.** Both boot the app and watch it behave, so
neither notices a broken exact-string expectation: `PerfBenchTests` asserts the JSON the benchmark
writes, character for character. Run `ctest --test-dir build -C Debug --output-on-failure` (or the
`Snowstorm-Tests` target) before pushing any change to a serialization or report format, since CI
runs it and a green smoke run says nothing about it.

## Scene capture gate (does it LOOK right)

Smoke proves a scene boots and settles; it cannot see a frame that is all magenta (the Runtime shown only a magenta sky on 2026-10-19 passed it). `Scripts/scene-capture-check.py` captures Sponza, Reflections, AlphaBlend and Pong headlessly and fails on two things: an error frame (one colour, magenta or black over most of the frame; no golden needed) and a FLIP above `--threshold` (0.10) against the committed golden `Scripts/scene-golden/<adapter-slug>/<scene>.npz`. A missing golden is exit 2 (SKIP), never a pass, though the error-frame check still runs. Each run writes PNGs to `Scripts/.scene-captures/`; `--update-golden` records new goldens, only after looking at those PNGs. `Scripts/test_scene_capture_check.py` covers the detector GPU-free. The gate has not run on a GPU yet and no golden is committed.

**A PR that changes rendering attaches before and after captures from the Z2** (the PNGs above, same scene, same adapter), and a showcase page shows a real capture of its scene, not a debug buffer.

**The Runtime resolves `startup.project` and `startup.scene` against the working directory first, then the engine root** (`ResolveStartupPath`), so a double-click works. A project, registry or scene it cannot load logs `[error]`, shows a message box (headless runs only log) and exits; it never renders an engine default sky over an empty world.

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
py Scripts/perf-bench.py --compare-exe <base-exe> --pairs 3 --gate   # same, exit 1 on a resolved regression
py Scripts/perf-bench.py --canary-pass Editor      # normalise out a global clock shift
py Scripts/perf-bench.py --gpu 5070         # pin the adapter on a multi-GPU box
```

The config matrix (`rt-off → shadows → +ao → +refl → +gi`) enables one RT effect at a time, so the
**Forward-pass ms delta between adjacent configs is that effect's cost**: the RT effects are inline in
the Forward pass, so this A/B *is* the per-effect timing (there's no separate GPU scope per effect, by
design: shadows, AO, GI and reflections are branches of one DefaultLit fragment shader, so a timestamp pair cannot separate them without splitting the draw). `Forward` carries nested rows (`depth` 1 in the JSON): `ForwardOpaque` (the batch flush, where the inline RT work lives), `Sky` and `Transparent`. A new row reads as a new pass to the gate until a baseline is re-captured. Two trailing configs sit outside that ladder, each repeating one rung with a different
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
adds a second denoise chain (the `ShadowSpec*` rows) the inline path has no analogue for. At Sponza's
light count, in ms over `rt-off` from the committed baselines: 9070 XT inline 2.272, stochastic 1.723
with that chain and 0.895 without; 5070 inline 1.294, stochastic 3.362 and 2.109. So the ranking
inverts across vendors rather than across configs: on AMD stochastic wins outright, on NVIDIA inline
wins by 2.6x. The spec chain alone is 0.828 ms on the 9070 XT and 1.253 ms on the 5070. Sub-0.05 ms
passes are ignored (timestamp noise).

**`StochasticOverlap.world` plus `camera-paths/stochastic-overlap.json` is the repro for #170.** A 16 m
corridor lit only by two shadow-casting point lights 3 m apart (range 9, so their falloffs overlap
over the whole mid-section), with six columns on the camera side and three crates behind, so each
occluder throws two crossing shadows and every penumbra edge sits in the overlap. The route strafes
past both lights at 1 m/s, 1.8 m from the back wall. Sponza's aisle route (#169) reaches the overlap
at one probe with the lights 7.2 m apart. Look at the raw estimator, since the a-trous masks the defect:

```
Snowstorm-Runtime --startup.scene=Projects/Sandbox/assets/scenes/StochasticOverlap.world \
  --camera.path --camera.path.file=Projects/Sandbox/assets/camera-paths/stochastic-overlap.json \
  --render.shadows.mode=2 --render.shadows.stochastic --render.shadows.denoise.iterations=0
```

Drop `--render.shadows.stochastic` for the inline control, which should show no seams. The scene has no
baselines and is not wired into `quality-motion.py`. It was authored without a GPU and has not been
rendered: that the artifact is visible in it is unconfirmed.

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

**On a machine that cannot pin clocks, the A/B against the base build is the gate; stored baselines are
references.** A re-run of the baseline's own commit on the Z2's 8060S fails with disjoint IQRs between
sittings, so the stored-baseline gate cannot be trusted there. `--compare-exe <base-exe> --pairs 3 --gate`
adds a verdict after the report: a config fails when the TOTAL or any pass is resolved (at least 2 pairs,
median paired delta larger than the pair-to-pair spread) AND over `--threshold` AND over `--abs-threshold`.
An inconclusive result passes, because the A/B could not show a regression. Exit 1 on a resolved
regression or a failed run. Without `--gate` the A/B stays an instrument and exits non-zero only when a run
fails, so a green exit then means "the runs completed", never "perf is fine". It prints one `VERDICT`
line per config and a closing PASS/FAIL. A failed run ends the log with `FAIL: a run failed during the A/B in: <config> (pair N A|B (exe), exit <code>|timeout <s>s|no JSON written)`, so the cause survives a truncated log tail.

**The two readouts use different verdict rules on purpose.** `compare` gates, so a finding must clear
`--abs-threshold` as well as the statistical test: materiality. `print_ab` measures, so it asks only
whether the pairs agree on size and sign: resolution. Giving the A/B a materiality floor would delete what
it exists to find. d74cb76's stride tradeoff is +0.034 ms on GIDenoise0 and -0.047 ms on GIDenoise2, both
under a 0.10 ms floor, and those two numbers ARE the finding; the full-res chains that survive such a
floor needed no A/B to see. Both readouts do share the 0.05 ms `NOISE_FLOOR_MS`, below which a percentage
means nothing either way.

`--canary-pass <name>` is the golden path's remaining correction: it scales the run by how much a pass
the change CANNOT affect moved against the baseline, cancelling a global clock shift that path has no
other way to see. It has no default on purpose, since naming a pass the change does affect silently
rescales away the result, and only the caller knows which is safe. It corrects a multiplicative shift
hitting everything alike, not contention landing unevenly.

Removing the noise at its source still beats averaging over it: a **stable power state** (AMD via the
Radeon Developer Tool Suite, NVIDIA via `nvidia-smi --lock-gpu-clocks`) is the real fix where the hardware
and tooling allow it.
`--pin-clocks` does it for NVIDIA: it locks the adapter selected by `--gpu` to its max graphics clock
(`--pin-clocks-mhz` lowers it if that throttles thermally) with `nvidia-smi --lock-gpu-clocks`, which needs an
elevated shell, and resets on exit including failure and Ctrl-C. AMD on Windows has no scriptable
mechanism, so there the flag prints `UNPINNED` in the header and changes nothing. Each JSON records
`clocksPinned`, and a diff against a baseline captured in the other state warns that the absolute numbers are
not comparable (a baseline predating the field counts as unpinned). `Scripts/test_perf_bench_pin.py` covers
the pure parts. Like
smoke, it needs a **real GPU** (Vulkan timestamps) so it's a **local** gate, not CI; on a device
without timestamp support the JSON sets `timestampsSupported:false` and the run counts as a SKIP
(exit **2**), never a pass and never a false FAIL.

**Baselines are keyed by adapter**: `Scripts/perf-baseline/<device-slug>/<config>.json`, where the slug
comes from the device name the run reports. GPU differences make ms non-comparable, so a run only ever
diffs against the set captured on the adapter it is running on, and a box with two cards keeps two
independent sets (the repo holds `amd-radeon-rx-9070-xt` and `nvidia-geforce-rtx-5070`, plus sets from other machines:
`amd-radeon-rx-7900-xtx`, which predates the resolution/pose stamping below and so has an unknown viewpoint,
and `amd-radeon-rx-9060-xt` and `amd-radeon-tm-8060s-graphics`, stamped at 1717 wide). Cross-vendor comparison is then a deliberate read
across two directories rather than an accidental mixture inside one set, which would turn every
cross-config delta into effect-cost plus hardware difference. Missing set = the gate prints the raw
numbers and exits **2** (SKIP), which is not a pass: it compared nothing, so it cannot claim one.
On a multi-GPU machine `SS_CONFIG_IGNORE` also discards the persisted
`render.gpu` pick, so selection falls back to auto and the adapter is whatever the driver enumerates
first; `--gpu` pins it, taking a short all-digits value as a candidate **index** and anything else
(including a model number like `9070`) as a case-insensitive name substring. Re-baseline deliberately
(with a commit) when a change *intends* to shift perf, never to paper over an unexplained regression.

**A baseline is keyed by resolution and viewpoint, and the script pins both.** `BENCH_CAMERA` feeds
`camera.override` and `BENCH_RESOLUTION` (1920x1080) feeds `render.resolution.width/height`, so both
live in the repo rather than in per-machine state. Every JSON still stamps `width`, `height` and the
6-value `camera`, and a mismatch is a SKIP (exit **2**) rather than a diff, since a different render
size moves *every* pass by roughly the same factor and reads as a global regression.

Resolution had to be pinned because nothing else pins it: the Editor renders at whatever size the
local ImGui dock layout gives its viewport panel, so a committed baseline silently stopped matching
when someone rearranged their editor, and the gate then compared nothing while still looking like it
ran. The committed sets had drifted to three different resolutions by accident of layout (1915x1064,
1717x979, 1677x999). `render.resolution.width/height` forces the render target size on both hosts
(0 = follow the window/panel, the default); the target is offscreen, so forcing a size needs no window
that big and the editor panel just displays the result scaled.

Changing `BENCH_CAMERA` or `BENCH_RESOLUTION` invalidates every baseline, so re-capture all adapters
in the same commit. A baseline predating these fields carries none of them and is compared without the
check; the `amd-radeon-rx-7900-xtx` set is in that state. The `amd-radeon-rx-9060-xt` and
`amd-radeon-tm-8060s-graphics` sets carry a 1717-wide stamp from before the pin, so they SKIP until re-captured.

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
reach full occupancy at <=96 VGPRs, but allocation granularity differs (12 vs 24), which moves where
the wave count steps: they disagree at 36 of the 256 possible VGPR counts, so neither model stands in
for the other. An unmodelled architecture yields no occupancy figure rather than a wrong one, which
disables the primary gate, so add a model before targeting a new family. It fails (exit 1) when
occupancy drops (fewer waves), a spill appears (0 to >0, hard fail), or LDS/ISA rises beyond
`--threshold` (default 10%). Raw VGPR% is intentionally not gated -- a VGPR rise that doesn't cross a
wave boundary costs nothing. The occupancy is *theoretical* and VGPR-only (LDS occupancy needs the
workgroup size RGA offline reports as 0); measure achieved occupancy with RGP. Baselines are committed
for `gfx1100` (RX 7900 XTX), `gfx1200` (RX 9060 XT) and `gfx1201`. On `gfx1200` exactly two of 52 shaders are
occupancy-limited, `DefaultLit.frag[inlineshadow]` (130 VGPRs, 10/16) and `GI.comp[restir]` (103, 12/16);
every other shader hits 16/16 and none carries scratch. `GIDenoise.comp` was the worst offender at 191 VGPRs / 8 waves until d74cb76 rolled its outer
a-trous loop.

**Occupancy is defined once, in `vgpr_occupancy()` here, and imported by anything else that needs it.**
`Scripts/shader-stats.py` briefly carried its own copy with an 8-VGPR granularity, which disagreed with
this model (12 on gfx11, 24 on gfx12) and scored `DefaultLit.frag[inlineshadow]` 12/16 against the gate's
10/16. Two tools disagreeing about the same hardware is worse than either being wrong on its own, and a
count near an allocation boundary is exactly where it shows: on gfx12 a shader past `5*24 = 120` VGPRs drops from 12 to 10 waves.

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
(`Tools/ShaderCook` includes `Render/ShaderCompile.hpp`, the same command-line builder the runtime
calls; the script builds it on first use with plain CMake. Profiles `vs/ps/cs_6_5`, entry `main`, `SS_RAYTRACING`/`SS_FP16` permutations)
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
trap: a `--update-baseline` from there deletes the entries for shaders no editor run exercises. Call-site
and dynamic permutations live in `Scripts/shader-permutations.txt`: add a line there when a call site
starts passing a new define. The flags need no sync, since the runtime and the cook share
`BuildDxcCommandLine`; `py Scripts/cook-shaders.py --dry-run` prints the planned command lines
without dxc. The runtime cache key (`ComputeShaderKey`, flags tag `v5_...`) length-prefixes and chains its fields, so define order matters and a repeated define cannot cancel; the dxc version is not in it yet, so clear `Engine/cache/shaders` after upgrading `Tools/dxc`. `cook-shaders.py` also replaced the stale `check_shaders.py` (which still
expected the old `#type` split and silently compiled nothing).

**This gate runs in CI** (`.github/workflows/shaders.yml`): unlike smoke-test and perf-bench, RGA is
static and needs no GPU, so hosted CI cooks the shaders and runs the occupancy gate on every shader
change. RGA is cached across runs (actions/cache) so only the first pays the download.

### Compiler feedback on shader PRs (information, not a gate)

`py Scripts/shader-feedback.py report --asic gfx1151,gfx1200 --spv-dir Engine/cache/shaders-cook --out head.json` writes RGA's per-shader numbers as JSON (VGPRs, SGPRs, LDS, scratch, spills, ISA bytes, ISA instruction count, theoretical waves/SIMD; worst case over permutations) per ASIC; `diff base.json head.json --markdown c.md` lists what changed. `.github/workflows/shader-feedback.yml` cooks base and head on every PR that touches `Engine/Shaders` and posts the diff as one updating comment. It is a static proxy for shortlisting ideas, never a merge gate; confirm a candidate with `perf-bench.py --compare-exe`. gfx1151 (the Z2's 8060S) reuses the gfx11 occupancy model; that RGA 2.14.2 compiles for gfx1151, and that RDNA3.5 shares the 1536-VGPR file, are unverified: an ASIC RGA rejects is listed under `errors` in the report. `Scripts/test_shader_feedback.py` covers the pure logic; the RGA runs and the ISA instruction parse have not run on real RGA output.

### Reading an RGP capture without the GUI

`rgp-capture.py` writes a `.rgp` only RGP's GUI can open. `py Scripts/rgp-read.py capture.rgp -o out.json` reads the legacy SQTT container (layout: AMD's MIT-licensed `sqtt_file_format.h`, GPUOpen-Drivers/pal, spec 1.6) into JSON: chunk inventory, ASIC info (offsets verified by compiling the header's struct), API info, clock calibration, PSO names, and per-frame GPU time summed from the queue-event submits (`gpuMs`, converted with the ASIC chunk's timestamp frequency). `--diff base.json cur.json` compares median frame GPU ms (percent and `--abs-threshold` floor, exit 1 on a rise). The SQTT_DATA token stream (achieved occupancy, instruction timing) has no public decoder and is listed but not decoded. An RDF-container file (RGP 2.x) exits 2 as unsupported. `Scripts/test_rgp_read.py` uses synthetic captures built from the documented layout: no real `.rgp` has been read, and whether the pinned RDTS build writes the legacy or the RDF container is unverified.

### Cross-vendor shader statistics (driver-reported; `--gate` is local and opt-in)

RGA is the *Radeon* GPU Analyzer, so the occupancy gate is structurally AMD-only and cannot see half of a
cross-vendor comparison. `Scripts/shader-stats.py` is the other half: `VK_KHR_pipeline_executable_properties`
asks whichever driver actually compiled the pipeline for its own statistics.

```
py Scripts/shader-stats.py --compare 9070 5070   # capture both adapters, cross-vendor table
py Scripts/shader-stats.py --gpu 5070            # one adapter
py Scripts/shader-stats.py --json <path>         # report an existing capture, no GPU run
py Scripts/shader-stats.py --compare 9070 5070 --frames 400 --update-baseline
```

**Coverage is what the run built, not the shader set.** Pipelines are created lazily, so a capture holds
only what the app compiled: two identical back-to-back runs on the 9070 XT gave 21 and 22 executables,
the second additionally creating DefaultLit's `[noinlineshadow]` permutation. RGA cooks every shader and
permutation offline and is deterministic, so **RGA stays the authority for AMD coverage and for anything
gated**; use this for NVIDIA, where no offline analyser exists, and for the cross-vendor comparison on
shaders present in both. `--frames 400` reliably builds the RT and denoise pipelines; the 90 default
leaves `GI.comp` and `Reflection.comp` out entirely.

`py Scripts/shader-stats.py --gate` (#168) diffs a capture, or `--json <path>`, against that baseline: exit 1 when a spill appears (0 to >0) or a register rise costs occupancy (AMD: fewer waves by `rga-occupancy.py`'s model; NVIDIA: over `--threshold` and at least 8 registers, an upper-bound model), exit 2 when nothing was compared. Withheld columns are never gated (so NVIDIA spills are not, today), and coverage drift is reported, not failed. Permutations share a (pipeline, stage) key and pair by sorted register count. `Scripts/test_shader_stats_gate.py` covers the pure logic; the gate has not run against a fresh GPU capture.

`Scripts/shader-stats-baseline/<device-slug>.json` is the committed, citable record. Without it these
numbers exist only in a gitignored `Engine/cache` file, which no reader can check.

Driven by the `shader.stats` CVar, which gates both the device extension and
`VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR`; that bit makes the driver retain compiler metadata and
slows pipeline creation, so a normal run never sets it and is unaffected (verified: perf `+gi` PASSes
unchanged). It needs the device, so it is local, and it does **not** replace RGA, which is static, gated,
and runs in CI on no GPU.

**Vendors report different things, and only registers survive the comparison.** AMD gives
`numUsedVgprs`/`numUsedSgprs`/`ldsUsageSizeInBytes`/`scratchMemUsageInBytes`; NVIDIA gives `Register
Count`/`Binary Size`/`Local Memory Size`/`Stack Size`. Only the register count and the spill indicator
mean the same thing, so only those are put side by side.

**A driver statistic can be wrong, so the tool classifies before reporting.** Constant-and-zero across
every executable is a real result (nothing spilled) and is noted. Constant-and-non-zero carries no
information. Constant non-zero HIGH 32 bits with varying low bits is a 32-bit value written into the
64-bit union without clearing the upper half, which the RTX 5070 does for `Local Memory Size`: every
value is `0x10_00000000 + {0,16,32,48}`. The low half is very likely the real spill size, and the column
is still **withheld** rather than masked, because the mask is inference and a plausible fabricated number
is worse than a missing one.

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

- **Runtime, not Editor.** Captures run `Snowstorm-Runtime` (no editor panels), and every capture
  forces `render.resolution.width/height` to **1920x1080** (`CAPTURE_RESOLUTION`, the override perf-bench
  pins), so the render size never follows the window (1177x649 and 1817x1009 were both observed in
  one session before #162). Metrics are still computed after a bilinear downscale to **1024x576**
  for cost, and as a safety net against a host that ignores the override. The cached PT references
  are keyed with `_REF_CACHE_VERSION` (bumped to 2 with this change), so captures made at window
  size are not reused. Committed quality baselines were captured at window size: re-capture them
  with `--update-baseline` on each reference machine; until then expect small metric shifts.
  `quality-motion.py` pins the same resolution for its route frames (and has its own cache version,
  3), so its committed motion baselines need the same re-capture.
- **Three Sponza viewpoints** (`atrium`, `floor`, `gallery`) share one position and vary orientation,
  covering content that stresses AO, GI and reflections differently. Two further candidate
  orientations were dropped as degenerate (yaw+pi was 99.7% dark; the side yaw 78.7% dark, 0%
  bright). Averaging across viewpoints is what `Scripts/quality-tune.py` minimizes, so a tuned
  parameter does not overfit one frame.
- **LPIPS is reported, not gated.** When `torch` and `lpips` are importable (`pip install torch lpips`),
  each technique line also prints LPIPS-AlexNet against the reference; otherwise a note says it is
  skipped. It is never auto-installed and not stored in the baselines, since it varies with the torch
  build and the weights download. In-engine LPIPS (#112) is not built: this is the offline path.
- **HDR-FLIP is reported, not gated (#157).** When a capture leaves `<path>_hdr.npy` (linear scene radiance
  from `v.SceneColor`, RGBA f2/f4, pre-exposure, so after TAA but before tonemap/FXAA/CAS), the script prints
  `HDR-FLIP` next to LDR FLIP and, with `--update-baseline`, stores `flipHdr` and the exposure range FLIP
  derived from the reference (`flipHdrParams`). Baselines without `flipHdr` are unaffected and the gate
  never reads it. LDR-FLIP stays: it is the image the user sees, including FXAA/CAS, which HDR excludes.
  Non-finite radiance is counted and zeroed with a warning. The HDR reference is cached beside the LDR one
  (`_REF_CACHE_VERSION` 3); a reference cached without HDR is re-captured once after an engine rebuild, in case
  the new build writes it. `Scripts/test_quality_bench_hdr.py` covers the metric plumbing on synthetic
  arrays. The engine writes `_hdr.npy` (next bullet) but it has not run on a GPU, so no
  HDR-FLIP number exists and the ordering against LDR-FLIP is unmeasured.
- **HDR-FLIP run-to-run spread (#157).** `py Scripts/hdr-flip-noise.py --ref <ref base> --runs <repeat bases...>` scores N repeat captures of one technique against one reference and prints the mean, stdev and range of `flipHdr`. That spread is the floor for any HDR-FLIP gate threshold; the script picks none and writes no baseline. `Scripts/test_hdr_flip_noise.py` covers it on synthetic arrays; no GPU capture has been measured with it.
- **HDR-FLIP under motion (#157).** `quality-motion.py` reports `flipHdr` per probe frame and as a mean (`HDR-FLIP ... (not gated)`), pairing each moving frame's `_f<N>_hdr.npy` with the HDR twin of its frozen-frame reference (`motion-f<N>__<key>_hdr.npy` in `Scripts/.quality-ref-cache`, written when the reference is captured). A reference cached before HDR capture has no twin and reads n/a until `--fresh-ref`. `flipHdr` is stored in the motion baseline JSON but never compared. `Scripts/test_quality_motion_hdr.py` covers the plumbing with stubbed metrics; no HDR number from a GPU run exists.
- **HDR capture (#157).** With capture on, the engine also writes `<path>_hdr.npy` beside `_ldr.npy` (motion frames: `_f<N>_hdr.npy`): `v.SceneColor` as the tonemap chain receives it, linear pre-exposure radiance, HxWx4, top-down, `<f2` for the RGBA16F real-time path and `<f4` for the RGBA32F path tracer. `render.exposure` is logged with it, not applied. It is a 1:1 copy into a texture the capture effect owns (`TransferSrc` on no hot target, no cost when capture is off) read back in the same pass as the LDR present, so the pair is one frame. It excludes everything after `SceneColor`: FXAA and CAS (TAA and the upscaler are included), so it cannot replace the LDR numbers. Not run on a GPU as of its PR; the pure format rules are `QualityCaptureFormat.hpp`, tested in `QualityCaptureFormatTests`.
- **`render.pathtrace.sampler` (#156) changes only the PT's low-sample error.** 0 (default) is PCG white noise seeded with the frame counter; 1 is Owen-scrambled 2D Sobol padded per decision (`Include/PtSampler.hlsli`, mirrored in `Render/QmcSampler.hpp` and tested by `QmcSamplerTests`), seeded per pixel only, so a Reset replays the same sequence. Fixed pair ids per decision keep an early-terminated path from shifting later dimensions. Every decision draws from it (pixel jitter, sun/point/spot/env NEE, lobe choice, BSDF, Russian roulette); `QmcSamplerTests` also fail when an integer constant in the shader copy is missing from the C++ mirror. The estimator and pdfs are unchanged, so the converged reference (and every committed quality baseline) is unaffected while the default stays 0; switching it restarts accumulation. Not yet run on a GPU, and the fixed-spp error ratio against PCG (the stop rule in the design) is unmeasured. `py Scripts/pt-convergence.py` produces it: per viewpoint, PT captures at N accumulated spp (1/4/16/64, `render.pathtrace.spp=1`, exact window of N frames) for each sampler, scored (FLIP, MSE) against the cached converged sampler-0 reference of `quality-bench.py`, with the PCG/Sobol ratio, the MSE log-log slope, a converged-equality read (mean-luma difference at the reference length, heuristic 0.5% tolerance) and the stop rule (FLIP ratio >= 1.5 at 4 and 16 spp). It gates nothing and writes no baseline. `Scripts/test_pt_convergence.py` covers the pure parts; the script has not run on a GPU.
- **Eleven techniques**: `raster`, `ssao`, `rtao`, `ssr`, `rtrefl`, `ssgi`, `rtgi`, `all-rt`, plus the
  shadow A/B `rtshadow`, `megalights` and `megalights-nospec`. Each is a full real-time render with that
  one technique on, TAA enabled; only `all-rt` and the shadow entries override the shadow mode.
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

**`Reflections.world` is the testbed for reflection changes.** Sponza cannot register any reflection
change (#171): one low-contrast specular floor moves only 0.3% of pixels by more than 2/255. The scene
puts the reflected signal where a metric can see it: a polished floor (roughness 0.04, metallic 0.9)
and a mirror wall (0.02, 1.0) facing saturated and checker-textured pillars, a black pillar and a
white one beside a bright procedural sky, a five-cube roughness ladder (0.05 to 0.7) for the
roughness cutoff and roughness-driven kernels, and the girl mesh for a non-box silhouette. Only
existing assets and per-entity overrides are used, so it needs no registry rows. It has no committed
baselines and no emissive surfaces (a material override cannot set emissive).

Every run sets `SS_CONFIG_IGNORE=1`, so a capture is code defaults plus the technique's overrides and
never this machine's persisted `SnowstormConfig.cfg`. Note that only `all-rt` and the
three shadow entries override `render.shadows.mode`: every other technique renders with the default
shadow map, so shadowing is a constant across them rather than something they measure.

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
at the identical pose, and measured that way (the moving frame against a static capture at the same
recorded pose, `all-rt`) the order is different: dolly 0.0887, strafe 0.1377, **reversal 0.1656**.
The `raster` row makes the trap explicit, since its `reversal` FLIP is the BEST of its four probes
while its motion penalty is the worst.

Velocity REVERSAL is therefore the costliest motion here, and it is not explained by speed or by
angular rate: arc-length reparameterisation holds translation at 0.0333-0.0340 units/frame across
every moving probe, and `strafe` carries 24x the yaw rate of `dolly` while costing less than the
reversal. What is special about the U-turn is that the velocity vector changes sign, which is the
documented pathological case for history rejection (FSR2 tracks high-velocity ghosting as its own
bug class). The `static` probe is the control that makes all of this readable: its penalty is an order
of magnitude below every moving probe (`all-rt` 0.0244 against 0.0887-0.1656), so the moving probes are
reporting motion rather than a protocol artifact.

**The `static` probe does NOT read zero, and should not be expected to.** Two reasons, both measured.
The camera parks but `Sponza-Motion.world`'s props keep rotating, so the world is still moving there
while the control freezes it. And the control accumulates while frozen whereas the parked capture only
recently stopped, which is worth ~0.002 of the residual: sweeping the control's accumulation over
25/100/400 frames moves `raster`'s static penalty 0.0132 -> 0.0152 -> 0.0156, saturating. Treat the
residual as the metric's noise floor, not as a validity check that must hit 0.

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
metric decides. Never optimise **motion penalty** either: it is the moving frame measured against this
renderer's own still, so an optimiser can drive it to zero by degrading the still.

**Its absolute scale is protocol-dependent, so never mix numbers across a capture-protocol change.**
60d154d clears temporal history on entering steady state whenever `quality.capture.frames > 0`, and
`run_motion_capture` sets that to 1 purely to arm sequence mode, so the clear applies to the moving
capture too. Before it, those frames inherited TAA and denoiser accumulation built up while assets were
still streaming, which made them artificially smooth and therefore artificially close to a converged
still; the penalty was suppressed roughly 4-10x as a result (`raster` dolly 0.0114 against 0.1197
after). The frozen control was unaffected, since it converges over 400 held frames either way. The
ORDERING across probes and techniques survived the rescale unchanged, which is what the metric is read
for; the absolute values did not.

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

### Speed tuning (cost-aware)

`Scripts/speed-tune.py --technique <rtgi|rtao|rtrefl|megalights|all-rt>` minimises perf-bench's `totalGpuMs` (median of `--repeat` runs of the matching rung) over the cost knobs `quality-tune.py` leaves out (rays, internal scale, denoise iterations), subject to quality under motion: JOD within `--jod-margin` (0.02) and tFLIP within `--tflip-margin` (0.001) of the engine defaults, measured in the same session against the same cached references. Search is differential evolution with Deb's feasibility rules, seeded, member 0 is the defaults. Output is a proposed settings diff with its measurements (and `--out` JSON), never a change to a default; a Perf PR built from it still needs a resolved `perf-bench.py --compare-exe <base-exe> --gate` result and green quality gates. Needs a GPU and ColorVideoVDP; the pure parts are covered by `Scripts/test_speed_tune.py`. It has not run on a GPU.
