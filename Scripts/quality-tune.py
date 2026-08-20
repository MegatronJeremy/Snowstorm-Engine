#!/usr/bin/env python3
"""Auto-tune real-time technique CVars against the path-traced reference (#161).

The quality gate (quality-bench.py) turns image quality into a number (FLIP vs the converged path
tracer). This script closes the loop: it treats that number as a black-box objective and searches a
technique's CVars for the setting that best matches the reference -- i.e. it tunes the real-time
renderer to look like ground truth, instead of hand-tuning by eye.

Method: capture the PT reference ONCE per viewpoint (expensive) and cache it; then each trial sets
candidate CVars via SS_RENDER_* env, runs the fast real-time capture, and computes mean FLIP across
all viewpoints (multi-viewpoint so the result doesn't overfit one view, #158). The optimizer is
coordinate descent with a per-parameter line search -- dependency-free and interpretable (you can read
exactly which value of each knob was searched and chosen), which suits a thesis better than an opaque
black box; swap in Optuna/CMA-ES later if sample-efficiency becomes the bottleneck.

The real-time vs reference gap is an un-occluded ambient shadow-fill (see #161), so the levers are the
ambient (render.ibl.intensity), the occlusion (AO radius/rays), and the indirect (render.gi.*). The
search is HARDENED against metric-gaming: an early run drove gi/ibl intensity to zero, which cuts FLIP
by darkening the image rather than fixing occlusion. So intensities are constrained to near-physical
bands (can't be dimmed away) and occlusion-quality knobs are the actual search dimension; a
boundary-clamped optimum is flagged. Reuses quality-bench.py's capture + FLIP (loaded as a module).

Needs a real GPU (the PT reference). Local, offline, slow-by-design (each trial is a headless capture).

Usage:
    py Scripts/quality-tune.py --technique rtgi                 # tune RT-GI's knobs
    py Scripts/quality-tune.py --technique ssao --rounds 3 --samples 7
    py Scripts/quality-tune.py --technique all-rt --frames 90 --ref-frames 300
"""
import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Reuse quality-bench.py wholesale (hyphenated filename -> load by path). Gives run_capture, flip,
# VIEWPOINTS, TECHNIQUES, REF_ENV, camera_env, DEFAULT_SCENE with zero duplication.
_spec = importlib.util.spec_from_file_location("quality_bench", Path(__file__).resolve().parent / "quality-bench.py")
qb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qb)

# Per-technique search space: (env var, lo, hi, is_int, start). `start` seeds coordinate descent.
#
# Hardened against metric-gaming (#161): the first version let the optimizer drive gi/ibl intensity to
# zero, which minimizes FLIP by DARKENING the image to match the reference's shadows instead of fixing
# the actual cause (the ambient's missing occlusion). So the intensity knobs are constrained to
# NEAR-PHYSICAL bands (they can't be dimmed away), and the real lever -- OCCLUSION quality (AO
# radius/rays, GI rays) -- is included so the optimizer reduces the shadow over-fill the correct way
# (occluding the ambient more accurately, not turning it off). A best value pinned to a band edge is
# flagged as a boundary-clamp warning (still suspect).
PARAM_SPACE = {
    "ssao": [
        ("SS_RENDER_AO_RADIUS", 0.2, 1.5, False, 0.5),      # occlusion extent -- the RIGHT lever
        ("SS_RENDER_AO_RAYS", 4, 32, True, 8),              # occlusion quality
        ("SS_RENDER_AO_INTENSITY", 0.75, 1.5, False, 1.0),  # near-physical band
        ("SS_RENDER_IBL_INTENSITY", 0.4, 1.0, False, 0.75), # constrained: can't be dimmed to 0
    ],
    "rtao": [
        ("SS_RENDER_AO_RADIUS", 0.2, 1.5, False, 0.5),
        ("SS_RENDER_AO_RAYS", 4, 32, True, 8),
        ("SS_RENDER_AO_INTENSITY", 0.75, 1.5, False, 1.0),
        ("SS_RENDER_IBL_INTENSITY", 0.4, 1.0, False, 0.75),
    ],
    "rtgi": [
        ("SS_RENDER_GI_RAYS", 1, 8, True, 2),               # GI quality -- occlude indirect the right way
        ("SS_RENDER_GI_INTENSITY", 0.8, 1.25, False, 1.0),  # near-physical band
        ("SS_RENDER_IBL_INTENSITY", 0.4, 1.0, False, 0.75),
    ],
    "all-rt": [
        ("SS_RENDER_GI_RAYS", 1, 8, True, 2),
        ("SS_RENDER_GI_INTENSITY", 0.8, 1.25, False, 1.0),
        ("SS_RENDER_AO_INTENSITY", 0.75, 1.5, False, 1.0),
        ("SS_RENDER_IBL_INTENSITY", 0.4, 1.0, False, 0.75),
    ],
}


def fmt(env: str, val: float, is_int: bool) -> str:
    return str(int(round(val))) if is_int else f"{val:.4f}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Auto-tune real-time CVars vs the path-traced reference.")
    ap.add_argument("--technique", required=True, choices=list(PARAM_SPACE), help="Which technique's CVars to tune")
    ap.add_argument("--rounds", type=int, default=2, help="Coordinate-descent passes over the parameter set")
    ap.add_argument("--samples", type=int, default=5, help="Line-search samples per parameter per round")
    ap.add_argument("--frames", type=int, default=60, help="Settle frames per real-time trial capture")
    ap.add_argument("--ref-frames", type=int, default=250, help="PT accumulation frames for the (cached) reference")
    ap.add_argument("--timeout", type=int, default=300, help="Per-capture wall-clock timeout in seconds")
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--scene", default=qb.DEFAULT_SCENE)
    args = ap.parse_args()

    build_dir = (ROOT / "build").resolve()
    exe = build_dir / f"Snowstorm-Runtime/{args.config}/Snowstorm-Runtime.exe"
    layer_path = (ROOT / "vcpkg" / "installed" / "x64-windows" / "bin").resolve()
    if not exe.exists():
        print(f"FAIL: executable not found at {exe}")
        return 1

    import tempfile
    tmp = Path(tempfile.gettempdir()) / "snowstorm-quality-tune"
    tmp.mkdir(parents=True, exist_ok=True)

    base_env = dict(qb.TECHNIQUES[args.technique])
    params = PARAM_SPACE[args.technique]

    print(f"Tuning '{args.technique}' over {[p[0] for p in params]}")
    print(f"Viewpoints: {list(qb.VIEWPOINTS)}   rounds={args.rounds} samples={args.samples}\n")

    # 1) Capture + cache the PT reference for each viewpoint (once). Runtime + camera.override per viewpoint.
    refs = {}
    for vp, pose in qb.VIEWPOINTS.items():
        print(f"[ref] {vp}: path tracer ({args.ref_frames} frames)...")
        img, _ = qb.run_capture({**qb.REF_ENV, **qb.camera_env(pose)}, tmp / f"{vp}_ref", args.ref_frames, exe, ROOT,
                                max(args.timeout, args.ref_frames // 2 + 60), layer_path, args.scene)
        if img is None:
            print(f"  reference capture FAILED for {vp}; aborting.")
            return 1
        refs[vp] = img

    # 2) Objective: mean FLIP across viewpoints for a candidate CVar set (cached by value tuple).
    cache: dict = {}
    evals = 0

    def objective(overrides: dict) -> float:
        nonlocal evals
        key = tuple(sorted(overrides.items()))
        if key in cache:
            return cache[key]
        flips = []
        for vp, pose in qb.VIEWPOINTS.items():
            env = {**base_env, **overrides, **qb.camera_env(pose)}
            img, _ = qb.run_capture(env, tmp / f"{vp}_trial", args.frames, exe, ROOT, args.timeout, layer_path, args.scene)
            if img is None or img.shape != refs[vp].shape:
                cache[key] = float("inf")
                return float("inf")
            flips.append(qb.flip(refs[vp], img))
        score = float(sum(flips) / len(flips)) if all(f is not None for f in flips) else float("inf")
        cache[key] = score
        evals += 1
        return score

    # 3) Coordinate descent from the seed values.
    best = {env: start for (env, _, _, _, start) in params}
    best_int = {env: is_int for (env, _, _, is_int, _) in params}
    best_str = {env: fmt(env, v, best_int[env]) for env, v in best.items()}
    base_score = objective({})  # engine defaults (no overrides) = the number to beat
    best_score = objective(best_str)
    print(f"\nbaseline (defaults) mean FLIP = {base_score:.4f}")
    print(f"seed {best_str} -> {best_score:.4f}\n")

    for r in range(args.rounds):
        for (env, lo, hi, is_int, _) in params:
            step = (hi - lo) / (args.samples - 1) if args.samples > 1 else 0.0
            for i in range(args.samples):
                val = lo + step * i
                trial = dict(best_str)
                trial[env] = fmt(env, val, is_int)
                s = objective(trial)
                tag = ""
                if s < best_score:
                    best_score = s
                    best_str = trial
                    tag = "  <- best"
                print(f"  r{r} {env}={trial[env]:>8}  meanFLIP={s:.4f}{tag}")
        print()

    print("=== Result ===")
    print(f"baseline defaults : {base_score:.4f}")
    print(f"tuned             : {best_score:.4f}  ({100.0 * (base_score - best_score) / base_score:+.1f}% FLIP)")
    print(f"evals             : {evals}")
    print("best CVars:")
    for env, v in best_str.items():
        cvar = env[3:].lower().replace("_", ".")  # SS_RENDER_GI_INTENSITY -> render.gi.intensity
        print(f"  {cvar} = {v}")

    # Boundary-clamp check: a best value pinned to a band edge means the true optimum is outside the
    # (deliberately near-physical) range -- likely the optimizer still trying to game the metric, or the
    # band being too tight. Flag it; the value should not be applied blindly.
    clamped = []
    for (env, lo, hi, is_int, _) in params:
        v = float(best_str[env])
        edge = (hi - lo) * 0.001 + 1e-6
        if v <= lo + edge:
            clamped.append(f"{env}={best_str[env]} (floor {lo})")
        elif v >= hi - edge:
            clamped.append(f"{env}={best_str[env]} (ceil {hi})")
    if clamped:
        print("\nWARNING: boundary-clamped -- optimum at a range edge (widen the band, add an occlusion knob, "
              "or the metric is still being gamed; do not apply blindly): " + ", ".join(clamped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
