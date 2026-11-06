#!/usr/bin/env python3
"""Run-to-run spread of HDR-FLIP (#157, the noise measurement that precedes gating it).

Takes one reference capture and N repeat captures of the same technique at the same viewpoint, scores each
repeat against the reference with quality-bench.py's flip_hdr, and prints the spread. A gate threshold below
this spread would rule on a difference the measurement cannot resolve, so the spread is the floor for any
HDR-FLIP threshold. Reports only; it picks no threshold and writes no baseline.

Captures are given as the base path of the engine's output, i.e. the file without the `_hdr.npy` suffix
(`<base>_hdr.npy` must exist). Needs flip-evaluator; exits 2 when it is missing or fewer than 2 repeats load.

  py Scripts/hdr-flip-noise.py --ref tmp/atrium_pt --runs tmp/r1/atrium_rtgi tmp/r2/atrium_rtgi tmp/r3/atrium_rtgi
"""
import argparse
import importlib.util
import statistics
import sys
from pathlib import Path


def load_quality_bench():
    spec = importlib.util.spec_from_file_location("quality_bench", Path(__file__).resolve().parent / "quality-bench.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def spread_stats(values):
    """Spread of repeated scores: n, mean, sample stdev (0 for n<2), min, max, range and range/mean.
    Raises ValueError on an empty list."""
    if not values:
        raise ValueError("no values")
    mean = statistics.fmean(values)
    lo, hi = min(values), max(values)
    return {
        "n": len(values),
        "mean": mean,
        "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": lo,
        "max": hi,
        "range": hi - lo,
        "relRange": (hi - lo) / mean if mean > 0 else 0.0,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", required=True, help="reference capture base (<base>_hdr.npy)")
    ap.add_argument("--runs", nargs="+", required=True, help="repeat capture bases of one technique")
    args = ap.parse_args(argv)

    qb = load_quality_bench()
    ref = qb.load_hdr(Path(args.ref))
    if ref is None:
        print(f"FAIL: {args.ref}_hdr.npy not found")
        return 1
    scores = []
    for run in args.runs:
        img = qb.load_hdr(Path(run))
        if img is None:
            print(f"  skip {run}: no _hdr.npy")
            continue
        if img.shape != ref.shape:
            print(f"  skip {run}: shape {img.shape} != reference {ref.shape}")
            continue
        r = qb.flip_hdr(ref, img)
        if r is None:
            print("SKIP: flip-evaluator unavailable")
            return 2
        scores.append(r[0])
        print(f"  {run}: HDR-FLIP={r[0]:.5f}")
    if len(scores) < 2:
        print("SKIP: need at least 2 scored repeats to measure a spread")
        return 2
    s = spread_stats(scores)
    print(f"HDR-FLIP over {s['n']} repeats: mean {s['mean']:.5f}, stdev {s['stdev']:.5f}, "
          f"range {s['range']:.5f} ({s['relRange'] * 100:.2f}% of mean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
