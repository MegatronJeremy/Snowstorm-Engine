#!/usr/bin/env python3
"""GPU shader/pass perf benchmark + regression gate for Snowstorm.

Runs the Editor headlessly once per RT-effect config (SS_PERF_BENCH_FRAMES), each of
which boots a scene, averages the render graph's per-pass GPU timings over a fixed
frame budget, and writes a JSON (see PerfBench.hpp). This script parses each JSON,
prints a per-pass table, and diffs against a committed baseline in
Scripts/perf-baseline/<device-slug>/ -- failing (exit 1) if any pass regresses beyond the
threshold. Baselines are keyed by adapter, so one checkout can hold a set per GPU and a run
only ever diffs against the set captured on the adapter it is running on.

The config matrix answers "what does each RT effect cost": it starts from all-RT-off and
enables shadows / +AO / +reflections / +GI in turn, so the Forward-pass ms delta between
adjacent configs is that effect's cost (the RT effects are inline in the Forward pass, so
per-effect timing IS the A/B across configs, not a separate GPU scope). A trailing `ssgi`
config repeats the last rung with the screen-space GI producer, so it diffs against `+refl`
(not against its neighbour) and gives the screen-space-vs-RT cost of the same effect.

Needs a real GPU (Vulkan timestamps), so it's a LOCAL gate like smoke-test.py, not CI.

Usage (from repo root or anywhere):
    py Scripts/perf-bench.py                    # run the matrix, diff vs baseline, PASS/FAIL
    py Scripts/perf-bench.py --update-baseline  # capture current results as the new baseline
    py Scripts/perf-bench.py --only rt-off      # run a single config
    py Scripts/perf-bench.py --frames 300       # more frames (less noise, slower)
    py Scripts/perf-bench.py --threshold 20     # regression tolerance % (default 15)
    py Scripts/perf-bench.py --scene <path>     # benchmark a different scene
    py Scripts/perf-bench.py --gpu 5070         # pin the adapter on a multi-GPU box

Exit code: 0 if every config is within threshold (or --update-baseline), 1 on a regression or
run failure, 2 if a config had no baseline for this adapter and so was never compared. Exit 2 is
not a pass: it means the gate did not run, which is what a fresh box or a new GPU sees.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# The RT-effect config matrix. Each entry: (name, env overrides). Baseline is all RT off; each
# subsequent config turns one more effect on so the Forward delta attributes per-effect cost.
# Env names follow the CVar->env mapping (dots->_, SS_ prefix). All use TAA (render.aa=2) since the
# RT effects need it for a clean result and that's the realistic configuration.
# `ssgi` is not another rung: it re-runs the `+gi` rung with the screen-space GI producer instead of
# the ray-traced one, so (ssgi - +refl) against (+gi - +refl) is the screen-space-vs-RT cost A/B.
CONFIGS = [
    ("rt-off",   {"SS_RENDER_SHADOWS_MODE": "1", "SS_RENDER_AO_MODE": "0", "SS_RENDER_REFLECTIONS_MODE": "0", "SS_RENDER_GI_MODE": "0"}),
    ("shadows",  {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "0", "SS_RENDER_REFLECTIONS_MODE": "0", "SS_RENDER_GI_MODE": "0"}),
    ("+ao",      {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "2", "SS_RENDER_REFLECTIONS_MODE": "0", "SS_RENDER_GI_MODE": "0"}),
    ("+refl",    {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "2", "SS_RENDER_REFLECTIONS_MODE": "2", "SS_RENDER_GI_MODE": "0"}),
    ("+gi",      {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "2", "SS_RENDER_REFLECTIONS_MODE": "2", "SS_RENDER_GI_MODE": "2"}),
    ("ssgi",     {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "2", "SS_RENDER_REFLECTIONS_MODE": "2", "SS_RENDER_GI_MODE": "1"}),
]

DEFAULT_SCENE = "Projects/Sandbox/assets/scenes/Sponza.world"


def find_repo_root(script_dir: Path) -> Path:
    return script_dir.parent


def run_config(name: str, env_overrides: dict, exe: Path, cwd: Path, frames: int,
               timeout: int, layer_path: Path, scene: str, gpu: str) -> dict | None:
    """Run one config headlessly; return the parsed perf JSON (or None on failure)."""
    out_path = Path(tempfile.gettempdir()) / f"perf-bench-{name}.json"
    if out_path.exists():
        out_path.unlink()

    env = os.environ.copy()
    env["SS_PERF_BENCH_FRAMES"] = str(frames)
    env["SS_PERF_BENCH_PATH"] = str(out_path)
    env["SS_STARTUP_SCENE"] = scene
    env["SS_RENDER_AA"] = "2"  # TAA: the realistic config the RT effects assume
    env["SS_VALIDATION_NONFATAL"] = "1"
    # Config isolation: run pure code-defaults + the env overrides below, ignoring this machine's persisted
    # SnowstormConfig.cfg. Without it a persisted setting (e.g. render.shadow.resolution=4096) leaks into every
    # config and silently skews the baseline diff -- the benchmark must depend only on code, not local settings.
    env["SS_CONFIG_IGNORE"] = "1"
    # On a multi-GPU box that isolation also discards the persisted render.gpu picker choice, so device
    # selection falls back to auto and a run can land on a different adapter than the baseline was
    # captured on. --gpu pins it (same substring/index syntax as the CVar).
    if gpu:
        env["SS_RENDER_GPU"] = gpu
    env.update(env_overrides)
    if layer_path and layer_path.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layer_path)

    try:
        proc = subprocess.run([str(exe)], cwd=str(cwd), env=env,
                              capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"  {name}: FAIL (timed out after {timeout}s)")
        return None
    if proc.returncode != 0:
        print(f"  {name}: FAIL (exit code {proc.returncode})")
        return None
    if not out_path.exists():
        print(f"  {name}: FAIL (no JSON written to {out_path})")
        return None

    data = json.loads(out_path.read_text())
    if not data.get("timestampsSupported", False):
        print(f"  {name}: SKIP (device has no GPU timestamps)")
        return None
    return data


def aggregate_runs(runs: list[dict]) -> dict:
    """Fold N independent runs of one config into a median-per-pass result, tagged with the observed spread.

    Repetition is the point: the dominant noise source is RUN-level (GPU clock/thermal state drifts between
    launches), which more frames INSIDE one run cannot average away. Measured on an RX 9060 XT, three
    identical back-to-back runs spread 8-12% on every pass while the per-pass minimum moved only ~3% -- the
    signature of DVFS, not workload. The median across runs rejects a single throttled outlier the way a
    mean cannot; `spreadPct` carries how noisy the sample actually was so the gate can refuse to rule on a
    difference smaller than its own measurement error.
    """
    base = dict(runs[0])
    if len(runs) == 1:
        return base

    def median(vals: list[float]) -> float:
        v = sorted(vals)
        n = len(v)
        return v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2.0

    merged: dict[str, dict] = {}
    for name in {p for r in runs for p in r.get("passes", {})}:
        samples = [r["passes"][name] for r in runs if name in r.get("passes", {})]
        avgs = [s.get("avgMs", 0.0) for s in samples]
        med = median(avgs)
        entry = dict(samples[0])
        entry["avgMs"] = med
        entry["minMs"] = median([s.get("minMs", 0.0) for s in samples])
        entry["spreadPct"] = ((max(avgs) - min(avgs)) / med * 100.0) if med > 0 else 0.0
        merged[name] = entry

    base["passes"] = merged
    totals = [r.get("totalGpuMs", 0.0) for r in runs]
    base["totalGpuMs"] = median(totals)
    tmed = base["totalGpuMs"]
    base["totalSpreadPct"] = ((max(totals) - min(totals)) / tmed * 100.0) if tmed > 0 else 0.0
    base["runs"] = len(runs)
    return base


def device_slug(device: str) -> str:
    """Filesystem-safe directory name for an adapter, e.g. 'AMD Radeon RX 9070 XT' -> 'amd-radeon-rx-9070-xt'."""
    slug = re.sub(r"[^a-z0-9]+", "-", device.lower()).strip("-")
    return slug or "unknown-device"


def baseline_path(repo_root: Path, device: str, name: str) -> Path:
    # Keyed by adapter, so one checkout can hold a baseline set per GPU (ms are not comparable across
    # hardware, and a set split across two adapters would turn every cross-config delta into effect cost
    # plus hardware difference). The directory IS the isolation: a run only ever diffs against the set
    # captured on the adapter it is running on.
    return repo_root / "Scripts" / "perf-baseline" / device_slug(device) / f"{name}.json"


def compare(name: str, current: dict, baseline: dict, threshold_pct: float) -> bool:
    """Print a per-pass table (baseline vs current, Δ%); return True if within threshold."""
    cur_passes = current.get("passes", {})
    base_passes = baseline.get("passes", {})
    all_names = sorted(set(cur_passes) | set(base_passes))

    print(f"  {'pass':<18} {'baseline':>10} {'current':>10} {'delta':>9}")
    ok = True
    for p in all_names:
        b = base_passes.get(p, {}).get("avgMs")
        c = cur_passes.get(p, {}).get("avgMs")
        if b is None:
            print(f"  {p:<18} {'--':>10} {c:>10.3f}   (new)")
            continue
        if c is None:
            print(f"  {p:<18} {b:>10.3f} {'--':>10}   (gone)")
            continue
        # Ignore sub-0.05ms passes: timestamp noise there swamps any % and would false-fail.
        if b < 0.05 and c < 0.05:
            print(f"  {p:<18} {b:>10.3f} {c:>10.3f}   ~0")
            continue
        delta = (c - b) / b * 100.0 if b > 0 else 0.0
        flag = ""
        # A delta smaller than this pass's own run-to-run spread is not a measurement, so do not call it a
        # regression: that is how a noise-dominated gate manufactures failures (and hides real ones behind
        # a threshold it cannot resolve). Reported as INCONCLUSIVE, which is not a pass.
        spread = cur_passes.get(p, {}).get("spreadPct")
        noisy = spread is not None and abs(delta) <= spread
        if delta > threshold_pct and not noisy:
            flag = "  REGRESSION"
            ok = False
        elif delta > threshold_pct and noisy:
            flag = f"  INCONCLUSIVE (spread {spread:.1f}%)"
            ok = False
        # FS invocations (overdraw metric, #pipeline-stats). Informational: a graphics pass's fragment-
        # shader invocations; divide by the pass's pixel count for overdraw. Not gated (it's a diagnostic).
        fi = cur_passes.get(p, {}).get("fragInvocations", 0)
        frag = f"  frags={fi / 1e6:.1f}M" if fi else ""
        print(f"  {p:<18} {b:>10.3f} {c:>10.3f} {delta:>+8.1f}%{flag}{frag}")

    bt, ct = baseline.get("totalGpuMs", 0.0), current.get("totalGpuMs", 0.0)
    dt = (ct - bt) / bt * 100.0 if bt > 0 else 0.0
    print(f"  {'TOTAL gpu':<18} {bt:>10.3f} {ct:>10.3f} {dt:>+8.1f}%")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="GPU perf benchmark + regression gate.")
    ap.add_argument("--frames", type=int, default=300, help="Frames averaged per config (default 300)")
    ap.add_argument("--repeat", type=int, default=3,
                    help="Independent runs per config, medianed (default 3). Run-level GPU clock drift is the "
                         "dominant noise source and repetition is the only thing that averages it out; 1 "
                         "restores the old single-shot behaviour and is faster but much noisier.")
    ap.add_argument("--timeout", type=int, default=120, help="Per-config wall-clock timeout in seconds")
    ap.add_argument("--config", default="Debug", help="Build config dir under build/ (default Debug)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default build)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet for the validation-layer path")
    ap.add_argument("--only", default=None, help="Run only this config (e.g. rt-off, +gi)")
    ap.add_argument("--threshold", type=float, default=15.0, help="Regression tolerance %% (default 15)")
    ap.add_argument("--scene", default=DEFAULT_SCENE, help="Scene to benchmark")
    ap.add_argument("--gpu", default="", help="Pin the GPU (render.gpu syntax: name substring or index)")
    ap.add_argument("--update-baseline", action="store_true", help="Write current results as the new baseline")
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = find_repo_root(script_dir)
    build_dir = (repo_root / args.build_dir).resolve()
    layer_path = (repo_root / "vcpkg" / "installed" / args.triplet / "bin").resolve()
    exe = build_dir / f"Snowstorm-Editor/{args.config}/Snowstorm-Editor.exe"

    if not exe.exists():
        print(f"FAIL: executable not found at {exe} (build first, or check --config)")
        return 1

    configs = CONFIGS
    if args.only:
        configs = [c for c in CONFIGS if c[0] == args.only]
        if not configs:
            print(f"No config named '{args.only}'. Known: {[c[0] for c in CONFIGS]}")
            return 1

    print(f"Repo root : {repo_root}")
    print(f"Build dir : {build_dir}  (config: {args.config})")
    print(f"Scene     : {args.scene}   Frames: {args.frames}   Threshold: {args.threshold}%")
    print(f"Mode      : {'UPDATE BASELINE' if args.update_baseline else 'compare vs baseline'}\n")

    all_ok = True
    ungated = []
    for name, env_overrides in configs:
        print(f"=== {name} ===")
        runs = []
        for attempt in range(max(1, args.repeat)):
            r = run_config(name, env_overrides, exe, repo_root, args.frames,
                           args.timeout, layer_path, args.scene, args.gpu)
            if r is None:
                break
            runs.append(r)
        if not runs:
            all_ok = False
            continue
        current = aggregate_runs(runs)
        if len(runs) > 1:
            print(f"  {len(runs)} runs, median; total spread {current.get('totalSpreadPct', 0.0):.1f}%")

        device = current.get("device", "")
        bp = baseline_path(repo_root, device, name)
        if args.update_baseline:
            bp.parent.mkdir(parents=True, exist_ok=True)
            bp.write_text(json.dumps(current, indent=2))
            print(f"  updated baseline: {bp.relative_to(repo_root)}  (device: {device or '?'})")
        elif bp.exists():
            baseline = json.loads(bp.read_text())
            if not compare(name, current, baseline, args.threshold):
                all_ok = False
        else:
            ungated.append(name)
            print(f"  NOT GATED: no baseline at {bp.relative_to(repo_root)} for "
                  f"'{device or 'unknown device'}' -- run with --update-baseline first.")
            # Still print the current numbers so the run isn't useless.
            for p, s in sorted(current.get("passes", {}).items()):
                print(f"    {p:<18} {s['avgMs']:>10.3f} ms")
        print()

    print("=== Summary ===")
    if not all_ok:
        print("FAIL (regression or run failure)")
        return 1
    if ungated:
        print(f"SKIP: no baseline on this adapter for {len(ungated)} config(s): {', '.join(ungated)}")
        print("      nothing was compared for those, so this run is not a pass. "
              "Capture them with --update-baseline.")
        return 2
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
