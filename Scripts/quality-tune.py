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

# And quality-motion.py for --motion: PROBES, probe_frames, run_motion_capture. Same loading trick.
_mspec = importlib.util.spec_from_file_location("quality_motion", Path(__file__).resolve().parent / "quality-motion.py")
qm = importlib.util.module_from_spec(_mspec)
_mspec.loader.exec_module(qm)

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


# Knobs the STATIC objective is measured blind to, searched only under --motion.
#
# The static tuner dropped every one of these as flat, and the comment on that drop says exactly why:
# each technique renders with TAA, render.taa.maxblend 0.97 accumulates over ~33 frames, and a
# 200-frame static capture therefore converges whatever the denoise chain does. The a-trous ends up
# filtering an already-clean image, and the static metric then scores it BACKWARDS, rewarding the
# removal of a filter that earns its keep under motion and on disocclusion. That is a gap in the
# measurement, not evidence the knobs do nothing, and the motion objective is what closes it.
#
# render.taa.depth_reject is deliberately absent. It is held at 0 by choice; the decoupled
# render.rt.depth_reject is the one to move for RT-denoiser disocclusion.
MOTION_PARAM_SPACE = {
    "rtgi": [
        ("SS_RENDER_GI_DENOISE_ITERATIONS", 0, 5, True, 3),      # a-trous passes: each doubles tap stride
        ("SS_RENDER_GI_DENOISE_VARIANCE", 0.0, 8.0, False, 4.0), # SVGF variance-guided luminance phi
        ("SS_RENDER_GI_TEMPORAL_MAXBLEND", 0.80, 0.99, False, 0.97), # history weight ceiling: lag vs noise
        ("SS_RENDER_RT_DEPTH_REJECT", 0.0, 0.2, False, 0.02),    # disocclusion reject for the RT denoisers
        ("SS_RENDER_RT_DEPTHSIGMA", 20.0, 100.0, False, 50.0),   # a-trous depth edge-stop
    ],
    "all-rt": [
        ("SS_RENDER_GI_DENOISE_ITERATIONS", 0, 5, True, 3),
        ("SS_RENDER_GI_TEMPORAL_MAXBLEND", 0.80, 0.99, False, 0.97),
        ("SS_RENDER_RT_DEPTH_REJECT", 0.0, 0.2, False, 0.02),
        ("SS_RENDER_TAA_MAXBLEND", 0.80, 0.99, False, 0.97),     # the dominant temporal knob for every technique
        ("SS_RENDER_AO_DENOISE_ITERATIONS", 0, 5, True, 3),
        ("SS_RENDER_REFLECTIONS_DENOISE_ITERATIONS", 0, 5, True, 3),
    ],
    "rtao": [
        ("SS_RENDER_AO_DENOISE_ITERATIONS", 0, 5, True, 3),
        ("SS_RENDER_AO_DENOISE_VARIANCE", 0.0, 8.0, False, 4.0),
        ("SS_RENDER_RT_DEPTH_REJECT", 0.0, 0.2, False, 0.02),
        ("SS_RENDER_TAA_MAXBLEND", 0.80, 0.99, False, 0.97),
    ],
    "rtrefl": [
        ("SS_RENDER_REFLECTIONS_DENOISE_ITERATIONS", 0, 5, True, 3),
        ("SS_RENDER_REFLECTIONS_DENOISE_VARIANCE", 0.0, 8.0, False, 4.0),
        ("SS_RENDER_RT_DEPTH_REJECT", 0.0, 0.2, False, 0.02),
        ("SS_RENDER_TAA_MAXBLEND", 0.80, 0.99, False, 0.97),
    ],
}


def fmt(env: str, val: float, is_int: bool) -> str:
    return str(int(round(val))) if is_int else f"{val:.4f}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Auto-tune real-time CVars vs the path-traced reference.")
    ap.add_argument("--technique", required=True, help="Which technique's CVars to tune")
    ap.add_argument("--motion", action="store_true",
                    help="Tune against quality-motion.py: fly the route and minimise mean FLIP over the "
                         "probe frames, instead of mean FLIP over the static viewpoints. Searches "
                         "MOTION_PARAM_SPACE, the temporal knobs a static capture cannot see.")
    ap.add_argument("--probes", default=None, help="Probe subset for --motion (default: all)")
    ap.add_argument("--objective", default="jod", choices=["jod", "flip"],
                    help="What --motion minimises. 'jod' (default) = ColorVideoVDP, negated so lower "
                         "is better. 'flip' = mean FLIP, kept for comparison: it is measurably the "
                         "WRONG objective for denoiser knobs, which it drives to zero (see below).")
    ap.add_argument("--rounds", type=int, default=2, help="Coordinate-descent passes over the parameter set")
    ap.add_argument("--samples", type=int, default=5, help="Line-search samples per parameter per round")
    ap.add_argument("--frames", type=int, default=60, help="Settle frames per real-time trial capture")
    ap.add_argument("--ref-frames", type=int, default=250, help="PT accumulation frames for the (cached) reference")
    ap.add_argument("--tech-maxframes", type=int, default=200, help="Hard frame cap for real-time trial captures "
                    "(they never converge; uncapped each burns the 3000-frame safety cap ~100s). Default 200 -> ~7s.")
    ap.add_argument("--param", action="append", metavar="CVAR_ENV",
                    help="Restrict the search to these parameters (repeatable, substring match on the env "
                         "name). The rest stay at their seed. For re-tuning one knob after a change that "
                         "only moved that knob's balance, instead of re-searching the whole space.")
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

    space = MOTION_PARAM_SPACE if args.motion else PARAM_SPACE
    if args.technique not in space:
        print(f"FAIL: no {'motion ' if args.motion else ''}parameter space for '{args.technique}'. "
              f"Known: {list(space)}")
        return 1
    base_env = dict(qb.TECHNIQUES[args.technique])
    params = space[args.technique]

    # --param narrows the SEARCH, not the override set: every parameter still ships its seed to the engine, so
    # a narrowed run and a full run evaluate the same configuration space, just with fewer axes moving.
    search = params
    if args.param:
        search = [p for p in params if any(sub.upper() in p[0] for sub in args.param)]
        if not search:
            print(f"FAIL: --param {args.param} matched none of {[p[0] for p in params]}")
            return 1

    print(f"Tuning '{args.technique}' over {[p[0] for p in search]}")
    if len(search) != len(params):
        print(f"  (held at seed: {[p[0] for p in params if p not in search]})")
    objective_desc = (f"moving route frames, {args.objective}" if args.motion
                      else f"static viewpoints {list(qb.VIEWPOINTS)}, flip")
    print(f"Objective : {objective_desc}   rounds={args.rounds} samples={args.samples}\n")

    # 1) Capture + cache the PT reference (once). Under --motion the references are per ROUTE FRAME, at
    # the poses the moving run recorded, so a probe frame is compared against ground truth at exactly the
    # viewpoint it was rendered from.
    if args.motion:
        probe_names = [s.strip() for s in args.probes.split(",")] if args.probes else list(qm.PROBES)
        if unknown := [n for n in probe_names if n not in qm.PROBES]:
            print(f"FAIL: unknown probe(s) {unknown}. Known: {list(qm.PROBES)}")
            return 1
        frames = qm.probe_frames(probe_names)
        print(f"Probes: {probe_names} -> route frames {frames}")

        # One reference-establishing flight at engine defaults gives the poses. They are technique- and
        # CVar-independent (the route is), so this is paid once for the whole search.
        _imgs, poses, _dev = qm.run_motion_capture(base_env, tmp / "tune_poses", exe, ROOT, args.timeout,
                                                   layer_path, args.scene, frames, 4000)
        if not poses:
            print("  FAILED to establish route poses; aborting.")
            return 1
        refs = {}
        for f in frames:
            img, _d, cached = qb.capture_reference(f"motion-f{f:06d}", poses[f], args.ref_frames, exe, ROOT,
                                                   max(args.timeout, args.ref_frames // 2 + 60), layer_path,
                                                   args.scene, tmp)
            print(f"[ref] route frame {f}: {'cached' if cached else f'path tracer ({args.ref_frames} frames)'}")
            if img is None:
                print(f"  reference capture FAILED for frame {f}; aborting.")
                return 1
            refs[f] = img

        def objective_motion(overrides: dict) -> float:
            """Quality under motion for a candidate CVar set. Lower is better.

            Absolute quality, deliberately NOT the motion penalty. Penalty is moving-minus-static, so an
            optimiser handed it could drive it to zero by making the static case worse.

            The default objective is ColorVideoVDP (negated), not FLIP, and that choice is measured
            rather than assumed. Tuning all-rt's temporal knobs against motion FLIP drove
            ao.denoise.iterations and reflections.denoise.iterations to 0 and both blend ceilings to
            their floors: FLIP rewards removing blur whether or not the camera is moving, so handed a
            denoiser it deletes it. This is the repo's own DLSS-selection lesson one metric up (PSNR
            favours the blurry image, so LPIPS decides there); here FLIP favours the sharp noisy image,
            so a perceptual spatio-temporal metric has to decide instead.
            """
            imgs, _p, _d = qm.run_motion_capture({**base_env, **overrides}, tmp / "tune_trial", exe, ROOT,
                                                 args.timeout, layer_path, args.scene, frames, 4000)
            if not imgs:
                return float("inf")
            if args.objective == "flip":
                vals = [qb.flip(refs[f], imgs[f]) for f in frames]
                return float("inf") if any(v is None for v in vals) else float(sum(vals) / len(vals))
            jod, _per = qm.colorvideovdp(imgs, refs, probe_names)
            if jod is None:
                print("  FAIL: ColorVideoVDP unavailable, so the jod objective cannot run. "
                      f"{qm.CVVDP_INSTALL}, or pass --objective flip.")
                return float("inf")
            return -float(jod)  # higher JOD is better, and the optimiser minimises

    refs_static = {}
    for vp, pose in ({} if args.motion else qb.VIEWPOINTS).items():
        img, _, cached = qb.capture_reference(vp, pose, args.ref_frames, exe, ROOT,
                                              max(args.timeout, args.ref_frames // 2 + 60), layer_path,
                                              args.scene, tmp)
        print(f"[ref] {vp}: {'cached' if cached else f'path tracer ({args.ref_frames} frames)'}")
        if img is None:
            print(f"  reference capture FAILED for {vp}; aborting.")
            return 1
        refs_static[vp] = img

    # 2) Objective: mean FLIP across viewpoints for a candidate CVar set (cached by value tuple).
    cache: dict = {}
    evals = 0

    def objective(overrides: dict) -> float:
        nonlocal evals
        key = tuple(sorted(overrides.items()))
        if key in cache:
            return cache[key]
        if args.motion:
            score = objective_motion(overrides)
            cache[key] = score
            evals += 1
            return score
        flips = []
        for vp, pose in qb.VIEWPOINTS.items():
            env = {**base_env, **overrides, **qb.camera_env(pose)}
            img, _ = qb.run_capture(env, tmp / f"{vp}_trial", args.frames, exe, ROOT, args.timeout, layer_path,
                                    args.scene, max_frames=args.tech_maxframes)
            if img is None or img.shape != refs_static[vp].shape:
                cache[key] = float("inf")
                return float("inf")
            flips.append(qb.flip(refs_static[vp], img))
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
    # Label the objective everywhere it is printed: a JOD score is negated so the optimiser can
    # minimise it, so an unlabelled negative number would read as a broken run.
    unit = "-JOD" if (args.motion and args.objective == "jod") else "mean FLIP"
    print(f"\nbaseline (defaults) {unit} = {base_score:.4f}")
    print(f"seed {best_str} -> {best_score:.4f}\n")

    for r in range(args.rounds):
        for (env, lo, hi, is_int, _) in search:
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
                print(f"  r{r} {env}={trial[env]:>8}  {unit}={s:.4f}{tag}")
        print()

    print("=== Result ===")
    print(f"baseline defaults : {base_score:.4f}")
    print(f"tuned             : {best_score:.4f}  ({100.0 * (base_score - best_score) / abs(base_score):+.1f}% {unit})")
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
    for (env, lo, hi, is_int, _) in search:
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
