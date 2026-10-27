#!/usr/bin/env python3
"""Fixed-spp error of the path tracer's sample generators against its converged reference (#156).

quality-bench.py scores real-time techniques against the path tracer; it cannot score the path tracer.
This does: per viewpoint, capture the PT at N accumulated samples per pixel for each
`render.pathtrace.sampler` (0 = PCG, 1 = Owen-scrambled Sobol) and report error against the converged
sampler-0 reference (the cache quality-bench.py shares), the PCG/Sobol ratio and the fitted log-log slope.

Stop rule from design/156-qmc.md: flip the default only if the error ratio at 4 and 16 spp is >= 1.5.

    py Scripts/pt-convergence.py                       # all viewpoints, N = 1 4 16 64
    py Scripts/pt-convergence.py --only atrium --spp 4 16
    py Scripts/pt-convergence.py --json out.json

Captures use render.pathtrace.spp=1 and an exact window of N frames after streaming settles, so N is the
accumulated sample count up to the capture's 1-2 frame readback offset (the engine logs the frame it wrote).
Needs a real RT GPU, so it is a local tool, not CI. It never writes a baseline and never gates: exit 0 when
a table was produced, 2 when nothing could be measured.
"""
import argparse
import importlib.util
import json
import math
import sys
import tempfile
from pathlib import Path

DEFAULT_SPP = (1, 4, 16, 64)
SAMPLERS = {"pcg": "0", "sobol": "1"}
STOP_RULE_RATIO = 1.5
STOP_RULE_SPP = (4, 16)
BIAS_TOLERANCE = 0.005  # relative mean-luminance difference at the converged frame count; unvalidated heuristic


def loglog_slope(ns, errs):
    """Least-squares slope of log(err) against log(N); white noise in the mean is -0.5 for RMSE (-1.0 for MSE)."""
    pts = [(math.log(n), math.log(e)) for n, e in zip(ns, errs) if n > 0 and e and e > 0]
    if len(pts) < 2:
        return None
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    den = sum((p[0] - mx) ** 2 for p in pts)
    if den == 0:
        return None
    return sum((p[0] - mx) * (p[1] - my) for p in pts) / den


def ratio(pcg, sobol):
    """PCG error over Sobol error: above 1 means Sobol is closer to the reference."""
    if pcg is None or sobol is None or sobol <= 0:
        return None
    return pcg / sobol


def stop_rule(ratios_by_spp):
    """(passed, reasons). Every spp in STOP_RULE_SPP must be measured and reach STOP_RULE_RATIO."""
    reasons = []
    for n in STOP_RULE_SPP:
        r = ratios_by_spp.get(n)
        if r is None:
            reasons.append(f"{n} spp not measured")
        elif r < STOP_RULE_RATIO:
            reasons.append(f"{n} spp ratio {r:.2f} < {STOP_RULE_RATIO}")
    return (not reasons), reasons


def mse(a, b, np):
    d = a[..., :3] - b[..., :3]
    return float((d * d).mean())


def mean_luma_rel_diff(a, b, np):
    ma, mb = float(a[..., :3].mean()), float(b[..., :3].mean())
    return abs(ma - mb) / max(mb, 1e-9)


def load_quality_bench():
    path = Path(__file__).with_name("quality-bench.py")
    spec = importlib.util.spec_from_file_location("quality_bench", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def fmt(x, spec):
    return "n/a" if x is None else format(x, spec)


def main():
    qb = load_quality_bench()
    np = qb.np
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--spp", type=int, nargs="+", default=list(DEFAULT_SPP), help="accumulated samples per pixel to score")
    ap.add_argument("--only", default=None, help="one viewpoint (atrium, floor, gallery)")
    ap.add_argument("--ref-frames", type=int, default=qb.REF_FRAMES_DEFAULT)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--triplet", default="x64-windows")
    ap.add_argument("--scene", default=qb.DEFAULT_SCENE)
    ap.add_argument("--fresh-ref", action="store_true")
    ap.add_argument("--json", default=None, help="write the table as JSON")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (repo_root / args.build_dir).resolve()
    layer_path = (repo_root / "vcpkg" / "installed" / args.triplet / "bin").resolve()
    exe = build_dir / f"Snowstorm-Runtime/{args.config}/Snowstorm-Runtime.exe"
    if not exe.exists():
        print(f"FAIL: executable not found at {exe} (build first, or check --config)")
        return 1
    views = qb.VIEWPOINTS
    if args.only:
        if args.only not in views:
            print(f"No viewpoint named '{args.only}'. Known: {list(views)}")
            return 1
        views = {args.only: views[args.only]}
    spps = sorted(set(args.spp))
    if min(spps) < 1:
        print("FAIL: --spp values must be >= 1")
        return 1

    tmp = Path(tempfile.gettempdir()) / "snowstorm-pt-convergence"
    tmp.mkdir(parents=True, exist_ok=True)
    print(f"Scene: {args.scene}   spp: {spps}   ref frames: {args.ref_frames}   (not run on a GPU unless a table follows)\n")

    report = {}
    for vp, pose in views.items():
        ref, dev, cached = qb.capture_reference(vp, pose, args.ref_frames, exe, repo_root,
                                                max(args.timeout, args.ref_frames // 2 + 60), layer_path,
                                                args.scene, tmp, fresh=args.fresh_ref)
        print(f"=== {vp}: {'cached' if cached else 'captured'} reference ===")
        if ref is None:
            print("  no reference (capture failed or adapter lacks ray tracing); skipped.\n")
            continue
        rows = {name: {} for name in SAMPLERS}
        for name, sid in SAMPLERS.items():
            for n in spps:
                env = {**qb.REF_ENV, **qb.camera_env(pose),
                       "SS_RENDER_PATHTRACE_SPP": "1", "SS_RENDER_PATHTRACE_SAMPLER": sid}
                img, _ = qb.run_capture(env, tmp / f"{vp}_{name}_{n}", 1, exe, repo_root, args.timeout,
                                        layer_path, args.scene, max_frames=n)
                if img is None or img.shape != ref.shape:
                    print(f"  {name} @ {n} spp: capture failed")
                    continue
                rows[name][n] = {"mse": mse(img, ref, np), "flip": qb.flip(ref, img)}
        # Converged-equality: both samplers at the reference length must agree with the PCG reference.
        bias = {}
        for name, sid in SAMPLERS.items():
            env = {**qb.REF_ENV, **qb.camera_env(pose), "SS_RENDER_PATHTRACE_SAMPLER": sid}
            img, _ = qb.run_capture(env, tmp / f"{vp}_{name}_conv", args.ref_frames, exe, repo_root,
                                    max(args.timeout, args.ref_frames // 2 + 60), layer_path, args.scene)
            if img is not None and img.shape == ref.shape:
                bias[name] = {"meanLumaRelDiff": mean_luma_rel_diff(img, ref, np), "flip": qb.flip(ref, img)}

        print(f"  {'spp':>4} {'FLIP pcg':>9} {'FLIP sobol':>10} {'ratio':>6} {'MSE pcg':>10} {'MSE sobol':>10} {'ratio':>6}")
        ratios = {}
        for n in spps:
            p, s = rows["pcg"].get(n), rows["sobol"].get(n)
            rf = ratio(p and p["flip"], s and s["flip"])
            rm = ratio(p and p["mse"], s and s["mse"])
            ratios[n] = rf
            print(f"  {n:>4} {fmt(p and p['flip'], '.4f'):>9} {fmt(s and s['flip'], '.4f'):>10} {fmt(rf, '.2f'):>6} "
                  f"{fmt(p and p['mse'], '.2e'):>10} {fmt(s and s['mse'], '.2e'):>10} {fmt(rm, '.2f'):>6}")
        slopes = {}
        for name in SAMPLERS:
            ns = [n for n in spps if n in rows[name]]
            slopes[name] = loglog_slope(ns, [rows[name][n]["mse"] for n in ns])
        print(f"  MSE log-log slope: pcg {fmt(slopes['pcg'], '.2f')}  sobol {fmt(slopes['sobol'], '.2f')} "
              f"(white noise: -1.00; steeper is better)")
        for name, b in bias.items():
            flag = "" if b["meanLumaRelDiff"] <= BIAS_TOLERANCE else "  <-- exceeds heuristic tolerance, check for bias"
            print(f"  converged {name}: mean-luma rel diff vs reference {b['meanLumaRelDiff']:.4%}, FLIP {fmt(b['flip'], '.4f')}{flag}")
        ok, why = stop_rule(ratios)
        print(f"  stop rule (FLIP ratio >= {STOP_RULE_RATIO} at {STOP_RULE_SPP} spp): {'MET' if ok else 'NOT MET: ' + '; '.join(why)}\n")
        report[vp] = {"device": dev, "rows": rows, "slopes": slopes, "converged": bias,
                      "stopRuleMet": ok, "stopRuleReasons": why}

    if not report:
        print("nothing measured")
        return 2
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
