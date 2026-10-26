#!/usr/bin/env python3
"""Cost-aware tuner: minimise GPU frame time with quality held at the defaults' level.

quality-tune.py leaves the cost knobs (rays, internal scale) out because its objective cannot see GPU
time, so it would drive them to the ceiling. Here time IS the objective, and quality is a constraint
instead: a candidate is feasible only while, under motion and against the same cached path-traced
references, ColorVideoVDP JOD stays within --jod-margin of the engine defaults and tFLIP rises by no
more than --tflip-margin. Both are measured in the same session as the candidate, so drift cancels.
No win that comes from cutting quality can pass: a cheaper setting that loses JOD or adds flicker is
infeasible by construction.

Search: differential evolution (rand/1/bin, Storn and Price 1997) with Deb's feasibility rules, so it
needs no penalty weight. Seeded and dependency-free. The first population member is always the
defaults. Time per candidate is perf-bench's per-pass JSON (`totalGpuMs`, median of --repeat runs of
the matching perf config), reached through perf-bench.py as a module; the judges are not modified.

Time is measured on perf-bench's scene (Sponza) while the quality constraint is measured on
Sponza-Motion, and the perf rung for rtao/rtrefl also enables the lower rungs, which the quality
technique does not. Both are fine for ranking candidates and are not a like-for-like cost.

Output is a PROPOSAL, never a change: a settings diff against the defaults with every measurement
(printed and written to --out as JSON). A Perf PR built from it still has to show a resolved
improvement in `perf-bench.py --compare-exe <base-exe> --gate` and pass the gates. The reported gain is
a screening estimate: the best of many noisy medians is biased upward, so the winner and the defaults
are re-measured once more, interleaved, and that second reading is the one reported.

Needs a real GPU and torch + ColorVideoVDP (the JOD constraint cannot be evaluated without it).

Usage:
    py Scripts/speed-tune.py --technique all-rt
    py Scripts/speed-tune.py --technique rtgi --pop 8 --generations 6 --jod-margin 0.03
    py Scripts/speed-tune.py --technique rtao --param AO_ --out Dataset/speed-tune-rtao.json
"""
import argparse
import importlib.util
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# Cost knobs: (env, lo, hi, is_int, default). Defaults are the engine's own (EngineCVars.cpp; the
# test_defaults_match_engine_cvars test parses them), so the
# first member of the population is a no-op. Quality knobs that cost nothing are quality-tune.py's.
COST_SPACE = {
    "rtgi": [
        ("SS_RENDER_GI_RAYS", 1, 4, True, 2),
        ("SS_RENDER_GI_SCALE", 0.25, 1.0, False, 0.5),
        ("SS_RENDER_GI_DENOISE_ITERATIONS", 0, 5, True, 3),
    ],
    "rtao": [
        ("SS_RENDER_AO_RAYS", 1, 4, True, 2),
        ("SS_RENDER_AO_SCALE", 0.25, 1.0, False, 0.5),
        ("SS_RENDER_AO_DENOISE_ITERATIONS", 0, 5, True, 3),
    ],
    "rtrefl": [
        ("SS_RENDER_REFLECTIONS_RAYS", 1, 4, True, 1),
        ("SS_RENDER_REFLECTIONS_DENOISE_ITERATIONS", 0, 5, True, 0),
    ],
    "megalights": [
        ("SS_RENDER_SHADOWS_DENOISE_ITERATIONS", 0, 5, True, 3),
    ],
}
COST_SPACE["all-rt"] = COST_SPACE["rtgi"] + COST_SPACE["rtao"] + COST_SPACE["rtrefl"][:1] + [
    COST_SPACE["rtrefl"][1]]

# The perf-bench rung that renders the same technique, so time and quality describe one configuration.
PERF_CONFIG = {"rtgi": "+gi", "all-rt": "+gi", "rtao": "+ao", "rtrefl": "+refl", "megalights": "shadows-stoch"}


def fmt(val: float, is_int: bool) -> str:
    return str(int(round(val))) if is_int else f"{val:.4f}"


def decode(vec, params) -> dict:
    """Unit-cube vector -> {env: formatted value}. Integer knobs round to the nearest step."""
    return {env: fmt(lo + min(1.0, max(0.0, u)) * (hi - lo), is_int)
            for u, (env, lo, hi, is_int, _d) in zip(vec, params)}


def default_vector(params) -> list:
    return [(d - lo) / (hi - lo) for (_e, lo, hi, _i, d) in params]


def violation(m: dict, base: dict, jod_margin: float, tflip_margin: float) -> float:
    """0 when the candidate holds quality, else how far past the margins it is (JOD points plus tFLIP).

    A missing metric is infinitely infeasible: an unmeasured quality can not be claimed to hold."""
    if m.get("jod") is None or m.get("tflip") is None:
        return float("inf")
    return (max(0.0, base["jod"] - m["jod"] - jod_margin)
            + max(0.0, m["tflip"] - base["tflip"] - tflip_margin))


def better(a: dict, b: dict) -> bool:
    """Deb's rules: feasible beats infeasible, two infeasible compare by violation, two feasible by time.

    Ties go to `b` (the incumbent), so a trial must be strictly better to replace its parent."""
    if a["violation"] == 0 and b["violation"] == 0:
        return a["ms"] < b["ms"]
    if a["violation"] == 0 or b["violation"] == 0:
        return a["violation"] == 0
    return a["violation"] < b["violation"]


def differential_evolution(evaluate, dim: int, seed_vec, pop_size: int, generations: int,
                           rng: random.Random, f: float = 0.6, cr: float = 0.8, log=print):
    """rand/1/bin DE over the unit cube. `evaluate(vec) -> {"ms", "violation", ...}`.

    Member 0 is `seed_vec` (the defaults); the rest are uniform. Returns (best, evaluations)."""
    pop = [list(seed_vec)] + [[rng.random() for _ in range(dim)] for _ in range(pop_size - 1)]
    fit = [evaluate(v) for v in pop]
    evals = list(zip(pop, fit))
    for g in range(generations):
        for i in range(pop_size):
            others = [j for j in range(pop_size) if j != i]
            a, b, c = (pop[j] for j in rng.sample(others, 3))
            forced = rng.randrange(dim)
            trial = [min(1.0, max(0.0, a[k] + f * (b[k] - c[k]))) if (rng.random() < cr or k == forced)
                     else pop[i][k] for k in range(dim)]
            tf = evaluate(trial)
            evals.append((trial, tf))
            if better(tf, fit[i]):
                pop[i], fit[i] = trial, tf
        best_i = min(range(pop_size), key=lambda k: (fit[k]["violation"] != 0, fit[k]["violation"] or fit[k]["ms"]))
        log(f"  gen {g + 1}/{generations}: best {fit[best_i]['ms']:.3f} ms, violation {fit[best_i]['violation']:.4f}")
    best_i = min(range(pop_size), key=lambda k: (fit[k]["violation"] != 0, fit[k]["violation"] or fit[k]["ms"]))
    return (pop[best_i], fit[best_i]), evals


def settings_diff(best_env: dict, params) -> list:
    """Only the knobs that moved off their default, as (env, default, proposed)."""
    out = []
    for env, _lo, _hi, is_int, d in params:
        if best_env[env] != fmt(d, is_int):
            out.append((env, fmt(d, is_int), best_env[env]))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Minimise GPU time with JOD and tFLIP held at the defaults' level.")
    ap.add_argument("--technique", required=True, help=f"One of {list(COST_SPACE)}")
    ap.add_argument("--jod-margin", type=float, default=0.02, help="Max JOD loss vs defaults (default 0.02)")
    ap.add_argument("--tflip-margin", type=float, default=0.001, help="Max tFLIP rise vs defaults (default 0.001)")
    ap.add_argument("--pop", type=int, default=8, help="DE population (>=4)")
    ap.add_argument("--generations", type=int, default=5)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--param", action="append", help="Restrict the search to knobs matching this substring")
    ap.add_argument("--probes", default=None, help="Probe subset (default: all)")
    ap.add_argument("--repeat", type=int, default=3, help="Perf runs per candidate (median)")
    ap.add_argument("--perf-frames", type=int, default=200)
    ap.add_argument("--ref-frames", type=int, default=None)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--scene", default=None, help="Quality scene (default: the motion scene)")
    ap.add_argument("--gpu", default="", help="Adapter pin, as perf-bench --gpu")
    ap.add_argument("--out", default=None, help="Write the proposal JSON here")
    args = ap.parse_args()

    if args.technique not in COST_SPACE:
        print(f"FAIL: no cost space for '{args.technique}'. Known: {list(COST_SPACE)}")
        return 1
    if args.pop < 4:
        print("FAIL: --pop must be at least 4 (DE draws three distinct others)")
        return 1

    pb = _load("perf_bench", "perf-bench.py")
    qb = _load("quality_bench", "quality-bench.py")
    qm = _load("quality_motion", "quality-motion.py")

    exe = (ROOT / "build" / f"Snowstorm-Runtime/{args.config}/Snowstorm-Runtime.exe").resolve()
    editor = (ROOT / "build" / f"Snowstorm-Editor/{args.config}/Snowstorm-Editor.exe").resolve()
    layer_path = (ROOT / "vcpkg" / "installed" / "x64-windows" / "bin").resolve()
    for p in (exe, editor):
        if not p.exists():
            print(f"FAIL: executable not found at {p}")
            return 1

    params = COST_SPACE[args.technique]
    if args.param:
        params = [p for p in params if any(s.upper() in p[0] for s in args.param)]
        if not params:
            print(f"FAIL: --param {args.param} matched no knob")
            return 1
    scene = args.scene or qm.MOTION_SCENE
    ref_frames = args.ref_frames or qb.REF_FRAMES_DEFAULT
    base_env = dict(qb.TECHNIQUES[args.technique])
    perf_name = PERF_CONFIG[args.technique]
    perf_env = dict(next(e for n, e in pb.CONFIGS if n == perf_name))

    import tempfile
    tmp = Path(tempfile.gettempdir()) / "snowstorm-speed-tune"
    tmp.mkdir(parents=True, exist_ok=True)

    probe_names = [s.strip() for s in args.probes.split(",")] if args.probes else list(qm.PROBES)
    frames = qm.probe_frames(probe_names)
    _i, poses, device = qm.run_motion_capture(base_env, tmp / "poses", exe, ROOT, args.timeout, layer_path,
                                              scene, frames, 4000)
    if not poses:
        print("FAIL: could not establish route poses")
        return 1
    refs = {}
    for fr in frames:
        img, _d, cached = qb.capture_reference(f"motion-f{fr:06d}", poses[fr], ref_frames, exe, ROOT,
                                               max(args.timeout, ref_frames // 2 + 60), layer_path, scene, tmp)
        print(f"[ref] route frame {fr}: {'cached' if cached else 'path tracer'}")
        if img is None:
            print(f"FAIL: reference capture failed for frame {fr}")
            return 1
        refs[fr] = img

    cache: dict = {}

    def measure(env: dict) -> dict:
        key = tuple(sorted(env.items()))
        if key in cache:
            return cache[key]
        imgs, _p, _d = qm.run_motion_capture({**base_env, **env}, tmp / "trial", exe, ROOT, args.timeout,
                                             layer_path, scene, frames, 4000)
        m = {"jod": None, "tflip": None, "ms": float("inf")}
        if imgs:
            ev = qm.evaluate(imgs, refs, probe_names)
            jod, _per = qm.colorvideovdp(imgs, refs, probe_names)
            m["jod"], m["tflip"] = jod, ev["tflip"]
        runs = []
        for _ in range(args.repeat):
            r = pb.run_config(perf_name, {**perf_env, **env}, editor, ROOT, args.perf_frames, args.timeout,
                              layer_path, pb.DEFAULT_SCENE, args.gpu)
            if r is None or r is pb.NO_TIMESTAMPS:
                runs = []
                break
            runs.append(r)
        if runs:
            m["ms"] = float(pb.aggregate_runs(runs)["totalGpuMs"])
        cache[key] = m
        return m

    defaults_env = {env: fmt(d, is_int) for env, _lo, _hi, is_int, d in params}
    base = measure(defaults_env)
    if base["jod"] is None or base["ms"] == float("inf"):
        print("FAIL: the defaults could not be measured (ColorVideoVDP missing, or a run failed): "
              f"jod={base['jod']} ms={base['ms']}. {qm.CVVDP_INSTALL}")
        return 1
    print(f"\ndefaults: {base['ms']:.3f} ms total GPU, JOD {base['jod']:.4f}, tFLIP {base['tflip']:.5f}")
    print(f"constraint: JOD >= {base['jod'] - args.jod_margin:.4f}, tFLIP <= {base['tflip'] + args.tflip_margin:.5f}\n")

    def evaluate(vec) -> dict:
        env = decode(vec, params)
        m = dict(measure(env))
        m["violation"] = violation(m, base, args.jod_margin, args.tflip_margin)
        m["env"] = env
        return m

    rng = random.Random(args.seed)
    (best_vec, best), evals = differential_evolution(evaluate, len(params), default_vector(params), args.pop,
                                                     args.generations, rng)

    diff = settings_diff(best["env"], params)
    if best["violation"] == 0 and diff:
        # Winner's curse: re-measure both fresh (uncached), interleaved, and report this reading.
        re_best, re_base = dict(best), dict(base)
        for label, env, tgt in (("candidate", best["env"], re_best), ("defaults", defaults_env, re_base)):
            cache.pop(tuple(sorted(env.items())), None)
            tgt.update(measure(env))
        best["screen_ms"], base["screen_ms"] = best["ms"], base["ms"]
        best.update({k: re_best[k] for k in ("ms", "jod", "tflip")})
        base.update({k: re_base[k] for k in ("ms", "jod", "tflip")})
        best["violation"] = violation(best, base, args.jod_margin, args.tflip_margin)
    gain = 100.0 * (base["ms"] - best["ms"]) / base["ms"]
    print("\n=== Proposal (not applied) ===")
    if best["violation"] != 0 or not diff:
        print("No feasible setting beats the defaults on time within the quality margins.")
    else:
        print(f"{best['ms']:.3f} ms vs {base['ms']:.3f} ms ({gain:+.1f}% GPU time, screening estimate from a re-measure; search reading {best['screen_ms']:.3f} vs {base['screen_ms']:.3f} ms), JOD {best['jod']:.4f} "
              f"(defaults {base['jod']:.4f}), tFLIP {best['tflip']:.5f} (defaults {base['tflip']:.5f})")
        for env, d, v in diff:
            print(f"  {env}: {d} -> {v}")
    print(f"evaluations: {len(cache)} distinct. Measured on {device or 'unknown adapter'}; confirm with "
          f"perf-bench --compare-exe --gate and the quality gates before any default changes.")

    if args.out:
        feasible = best["violation"] == 0 and bool(diff)
        Path(args.out).write_text(json.dumps({
            "technique": args.technique, "device": device, "perfConfig": perf_name,
            "margins": {"jod": args.jod_margin, "tflip": args.tflip_margin},
            "defaults": {"env": defaults_env, **{k: base[k] for k in ("ms", "jod", "tflip")}},
            "proposal": {"feasible": feasible, "env": best["env"], "diff": diff,
                         **{k: best[k] for k in ("ms", "jod", "tflip")}},
            "candidates": [{"env": decode(v, params), "ms": r["ms"], "jod": r["jod"], "tflip": r["tflip"],
                            "violation": r["violation"]} for v, r in evals],
        }, indent=2, default=str))
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
