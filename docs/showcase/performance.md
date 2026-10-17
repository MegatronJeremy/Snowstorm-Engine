# Performance

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza); every number below is read from a committed baseline file.*

Three gates cover performance, none needing the others: `Scripts/perf-bench.py` (GPU time per render-graph pass), its paired `--compare-exe` A/B (does a change cost anything) and `Scripts/rga-occupancy.py` (static register pressure). `Scripts/shader-stats.py` adds driver-reported statistics. The first two need a real GPU and run locally; the RGA gate needs none and runs in CI.

## Config ladder: cost per effect

`perf-bench.py` runs the Editor headlessly per config, 300 frames each after a detected steady state, at the pinned 1920x1080 and camera. The ladder enables one RT effect at a time, so the delta in total GPU time between adjacent rungs is that effect's cost.

Source: `Scripts/perf-baseline/<device-slug>/<config>.json`, field `totalGpuMs`, median of 5 runs. The 9070 XT and 5070 sets were last committed in b253f91 (2026-09-08); master is 5a215c5 as of 2026-10-17. Sponza, Debug build.

| Config | RX 9070 XT total (ms) | step (ms) | RTX 5070 total (ms) | step (ms) |
| --- | --- | --- | --- | --- |
| `rt-off` | 2.375 | | 1.319 | |
| `shadows` (RT shadows, mode 2) | 4.666 | +2.292 | 2.845 | +1.526 |
| `+ao` | 5.525 | +0.859 | 3.621 | +0.775 |
| `+refl` | 7.274 | +1.749 | 4.742 | +1.121 |
| `+gi` | 8.991 | +1.717 | 6.155 | +1.413 |

Steps are differences of the stored totals, rounded to three places. Total frame cost of the full ladder over `rt-off`: 6.616 ms on the 9070 XT, 4.836 ms on the 5070. Shadows are the largest step on both, but the others are not proportional across vendors (GI is 1.717 ms on the 9070 XT against 1.413 ms on the 5070, shadows 2.292 against 1.526), so neither column stands in for the other. Per-pass detail is in the same files: at `+gi`, `Forward` is 4.534 ms (9070 XT) and 2.553 ms (5070), then `Reflection` 1.475 / 1.116 and `GI` 1.069 / 0.721. Because the RT effects are inline in Forward, the Forward delta between rungs is the per-effect timing; there is no separate scope per effect.

Two configs sit outside the ladder and repeat one rung with a different producer:

| Config | Repeats | RX 9070 XT (ms) | RTX 5070 (ms) |
| --- | --- | --- | --- |
| `ssgi` | `+gi` with screen-space GI | 8.037 | 5.662 |
| `shadows-stoch` | `shadows` with the stochastic pass (experimental) | 4.111 | 4.660 |
| `ssao` | AO, screen-space | 5.171 | 3.181 |
| `ssr` | reflections, screen-space | 6.038 | 4.083 |

Stochastic shadows beat inline on AMD (4.111 against 4.666 ms) and lose on NVIDIA (4.660 against 2.845 ms), the cross-vendor inversion described in [`AGENTS.md`](../../AGENTS.md). Its own figures there (in ms over `rt-off`, 9070 XT inline 2.272, stochastic 1.723; 5070 inline 1.294, stochastic 3.362) are quoted separately and are not derived from the totals above.

Other committed sets, different conditions: RX 9060 XT (last commit b65ffd8, 2026-08-24) and Radeon 8060S of the Z2 build machine (2a142bb, 2026-10-14) were captured at 1717 wide, and the RX 7900 XTX set (5b3176b, 2026-08-22) predates resolution stamping. Their totals at `+gi`: 16.713, 40.370 and 9.236 ms. They are not comparable with the 1920x1080 sets, and the gate reports SKIP rather than diffing across a resolution mismatch.

### Reading the gate

- Failure needs a regression beyond both `--threshold` (15%) and `--abs-threshold` (0.10 ms).
- Each pass carries a quartile interval (`q1Ms`, `q3Ms`); disjoint intervals with the current run higher is a regression, overlap reads INCONCLUSIVE.
- Baselines are keyed by adapter; a missing set or a resolution/camera mismatch exits 2 (SKIP), never 0.
- The dominant noise is run-level (clock drift between launches), not frame-level, hence the median over `--repeat` runs.

## Paired A/B: measuring a change

```
py Scripts/perf-bench.py --compare-exe <base-exe> --pairs 3 --gate
```

Builds A and B alternate (A,B,A,B,...) in one session, so common-mode drift cancels, and the report is the median per-pair delta with the pair-to-pair spread. Without `--gate` it is an instrument: it asks only whether pairs agree on size and sign. With `--gate` a config fails when the total or any pass is resolved (at least 2 pairs, median paired delta larger than the spread) and over both thresholds; an inconclusive result passes. It exists because a stored baseline cannot be corrected for drift: on the Z2's 8060S, re-running the baseline's own commit fails with disjoint IQRs between sittings. The committed 8060S `shadows-stoch` set has 5 runs where its other configs have 3 (see the `runs` field), so even that set is not uniform.

## Shader occupancy (RGA, static)

`Scripts/rga-occupancy.py` compiles each SPIR-V module through the Radeon GPU Analyzer and gates derived waves per SIMD, spills (0 to >0 is a hard fail) and LDS/ISA growth. Source: `Scripts/rga-baseline/occupancy-gfx1200.json` (RX 9060 XT class) and `occupancy-gfx1100.json` (RX 7900 XTX), last committed in 2a408d9 (2026-10-15).

| ASIC | Shaders in baseline | Highest VGPR count | Shaders with VGPR spills |
| --- | --- | --- | --- |
| `gfx1200` | 49 | 130 (`DefaultLit.frag[inlineshadow]`) | 0 |
| `gfx1100` | 49 | 135 (`DefaultLit.frag[inlineshadow]`) | 0 |

These counts come from the baseline files as committed; the prose in `AGENTS.md` quotes older figures (47 shaders, 127 VGPRs) and the files are the record. Reproduce the CI input with `py Scripts/cook-shaders.py --variants rt,base` then `py Scripts/rga-occupancy.py --spv-dir Engine/cache/shaders-cook`. Occupancy is theoretical and VGPR-only; achieved occupancy needs RGP. RGA is AMD-only.

## Driver-reported statistics

`py Scripts/shader-stats.py --compare 9070 5070` asks each driver for `VK_KHR_pipeline_executable_properties` statistics, the only route to NVIDIA register counts. The committed `Scripts/shader-stats-baseline/amd-radeon-rx-9070-xt.json` holds 26 executables; coverage is whatever the run built, so RGA stays the authority for AMD coverage. `--gate` diffs against it locally; per `AGENTS.md` it has not run against a fresh GPU capture. The RTX 5070 reports `Local Memory Size` with a constant high 32 bits, so that column is withheld rather than masked.

## Reproduce

```
py Scripts/perf-bench.py            # ladder, diff vs Scripts/perf-baseline/<slug>
py Scripts/perf-bench.py --only +gi
```

Compare `totalGpuMs` in your output with the files named above; the table's steps are subtractions of those fields. Numbers from another adapter diff against that adapter's own directory, or SKIP.
