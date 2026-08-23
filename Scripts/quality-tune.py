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

The residual real-time vs reference gap is an un-occluded ambient shadow-fill (see #161), so the levers
are the occlusion (AO radius/rays, denoiser edge-stop) and the indirect (render.gi.*).
Not every part of that gap is tunable: the larger half of it was a missing 1/PI Lambertian
normalization on the RT secondary-hit sun term, which no CVar could reach. Treat a knob that refuses to
close a gap as evidence of a bug in the technique, not as a band that needs widening. The
search tunes OCCLUSION-QUALITY knobs ONLY (ray counts, AO radius, gather range, denoiser) at FIXED
physical intensities. Intensity is deliberately NOT a lever: runs showed the optimizer always dims gi/ibl to
darken the (structurally over-bright) real-time indirect toward the reference's shadows -- a metric-
gaming shortcut, not a quality gain (even near-physical bands pinned to their floors). With brightness
fixed, any FLIP gain is legitimately better occlusion; no gain is the honest signal that the gap is
structural (#157). A boundary-clamped optimum is still flagged. Reuses quality-bench.py (as a module).

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
# OCCLUSION-ONLY, deliberately (#161). The first version let the optimizer tune intensity (gi/ibl/ao),
# and it always drove them down to darken the image toward the reference's shadows -- a metric-gaming
# "turn the lights down" that isn't a quality improvement (see the boundary-clamp finding). Constraining
# to near-physical bands didn't help: the optima still pinned to the floors. The real lesson: brightness
# must NOT be a tuning lever, because dimming the (structurally over-bright) real-time indirect always
# improves the average match. So the tuner only searches OCCLUSION-QUALITY knobs (ray counts, AO radius,
# denoiser) at FIXED physical intensities -- any FLIP gain is then legitimately better occlusion, and if
# there is none, that is the honest signal that the gap is structural (the real-time indirect is
# un-occluded / over-bright vs the reference's true visibility -- the #157/structural item, not a CVar).
PARAM_SPACE = {
    "ssao": [
        ("SS_RENDER_AO_RADIUS", 0.2, 1.5, False, 0.5), # occlusion extent
        ("SS_RENDER_AO_RAYS", 4, 32, True, 8),         # occlusion quality (noise)
    ],
    "rtao": [
        ("SS_RENDER_AO_RADIUS", 0.2, 1.5, False, 0.5),
        ("SS_RENDER_AO_RAYS", 4, 32, True, 8),
    ],
    "rtgi": [
        ("SS_RENDER_GI_RAYS", 1, 8, True, 2),                  # GI quality (noise/occlusion of indirect)
        ("SS_RENDER_GI_DENOISE_VARIANCE", 0.5, 4.0, False, 2.0), # denoiser strength (bias vs noise)
    ],
    "all-rt": [
        ("SS_RENDER_GI_RANGE", 4.0, 30.0, False, 17.0),             # indirect gather distance; the seed matches the engine
                                                                     # default, whose description carries the measurement.
                                                                     # GI.comp sets ray.TMax = GIRange and RayQuery cannot
                                                                     # distinguish "left the scene" from "ran out of TMax", so
                                                                     # both land in the sky-cube else branch: too short a range
                                                                     # feeds un-occluded sky to rays that died in mid-air inside
                                                                     # a closed interior. SSGI reads the same CVar as its march
                                                                     # step (GIRange / 32), so it is not an RT-only knob.
        ("SS_RENDER_AO_RADIUS", 0.2, 3.0, False, 0.5),              # occlusion extent, CAPPED at ~3 physical: beyond that AO
                                                                     # becomes a global dimmer (gaming, #161). Near-flat in
                                                                     # isolation now (0.5 -> 0.2 is worth ~0); the band above
                                                                     # 0.5 measurably hurts, so the cap is the load-bearing part.
        ("SS_RENDER_GI_SPEC_AMBIENT_FADE", 0.0, 1.0, False, 1.0),  # #163 env-spec occlusion (validated ->1.0 three times)
        ("SS_RENDER_SHADOW_SUN_ANGLE_DEG", 0.1, 5.0, False, 1.0),  # sun soft-shadow penumbra width vs the PT's real sun angle.
                                                                     # Reaches the default inline path via frame.SunCosThetaMax;
                                                                     # shadows.normalbias does NOT (stochastic pass only), so it is absent.
        ("SS_RENDER_REFLECTIONS_MAX_ROUGHNESS", 0.3, 1.0, False, 0.8), # glossy/env cutoff: traced radiance instead of the
                                                                     # un-occluded env cube, the #163 theme. Near-flat standalone
                                                                     # (+0.8%, inside the +-3% noise floor); kept as an occlusion knob.
        ("SS_RENDER_GI_BOUNCE_AMBIENT", 0.0, 1.0, False, 0.5),     # un-occluded IBL ambient injected at each RT-GI secondary hit.
                                                                     # A constant-visibility approximation, so it is an occlusion knob,
                                                                     # not an intensity dimmer: the floor (0.0) is the physical end,
                                                                     # matching the path tracer, which injects no free ambient per bounce.
                                                                     # Searches interior and symmetric around 0.5, confirming the default.
        # DROPPED, measured flat on this metric: gi.denoise.iterations (0..5 spans 0.56% mean FLIP),
        # gi.denoise.variance (4.0 -> off, 0.07%), rt.depthsigma (0.2% across its whole 20..100 band, both rounds).
        # One cause covers all three: every technique here renders with TAA, and render.taa.maxblend 0.97
        # accumulates the final image over ~33 frames, so a 200-frame static capture converges whatever the GI
        # chain does (disabling GI temporal accumulation outright moves mean FLIP by 0.3%). The a-trous then
        # filters an already-clean image, and the static metric scores it BACKWARDS: with temporal off,
        # iterations=0 wins on FLIP (-3.0%) and PSNR (+0.09dB) while losing SSIM (-0.0024), the signature of
        # removed blur. An optimizer handed these would delete a filter that earns its keep under motion and on
        # disocclusion, which a static capture cannot reach (#159).
        # EXCLUDED BY DESIGN, same reasoning as intensity: the resolution knobs (gi.scale, ao.scale) and the sample
        # counts (gi.rays, ao.rays) would be driven straight to the ceiling for a 4x cost the static metric cannot
        # see (gi.rays 2->8 measured +4.3% FLIP for 4x the rays). Buying FLIP with GPU time is the mirror image of
        # buying it with brightness; that belongs in perf-bench. render.sharpen is excluded for the sharper version
        # of the same reason: it is the one knob measured to move FLIP and fidelity in OPPOSITE directions (+0.5%
        # FLIP but -0.44dB PSNR, -0.018 SSIM), so leaving it in lets the optimizer trade real fidelity for a
        # perceptual-metric win.
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
    ap.add_argument("--tech-maxframes", type=int, default=200, help="Hard frame cap for real-time trial captures "
                    "(they never converge; uncapped each burns the 3000-frame safety cap ~100s). Default 200 -> ~7s.")
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
        img, _, cached = qb.capture_reference(vp, pose, args.ref_frames, exe, ROOT,
                                              max(args.timeout, args.ref_frames // 2 + 60), layer_path,
                                              args.scene, tmp)
        print(f"[ref] {vp}: {'cached' if cached else f'path tracer ({args.ref_frames} frames)'}")
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
            img, _ = qb.run_capture(env, tmp / f"{vp}_trial", args.frames, exe, ROOT, args.timeout, layer_path,
                                    args.scene, max_frames=args.tech_maxframes)
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
    # Env form, not a derived CVar name: the CVar->env mapping (dots -> '_') is not reversible, and several
    # CVars carry underscores of their own (render.gi.bounce_ambient), so a derived name would name nothing.
    print("best (env form):")
    for env, v in best_str.items():
        print(f"  {env}={v}")

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
