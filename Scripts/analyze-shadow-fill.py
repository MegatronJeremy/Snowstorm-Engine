#!/usr/bin/env python3
"""Bisect the residual real-time-vs-PT over-brightness in shadow (post-#163 follow-up, task #39).

The auto-tuner keeps driving ao.radius to whatever ceiling it is given (a global dimmer), which means
there is still an un-occluded term over-filling shadows beyond the #163 env-spec fix. This isolates it:
capture the cached PT reference and a matrix of real-time all-rt configs that each ZERO one indirect
term, then report, per config, the shadow-band fill ratio (mean real-time luminance / mean PT luminance
over pixels the PT renders dark) and a global luminance fit (realtime ~= a*PT + b). The config that
collapses the shadow fill ratio toward the PT is the culprit term. Data first, no shader change yet.

Runs at one viewpoint (atrium) by default; the PT ref is reused from the quality-bench disk cache.
"""
import importlib.util
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("quality_bench", ROOT / "Scripts" / "quality-bench.py")
qb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qb)


def lum(img):  # img [H,W,4] in [0,255] -> luminance in [0,1]
    return (img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114) / 255.0


def analyze(ref, test, dark=0.15, mid=0.5):
    lr, lt = lum(ref), lum(test)
    a, b = np.polyfit(lr.ravel(), lt.ravel(), 1)  # test ~= a*ref + b
    shadow = lr < dark
    midm = (lr >= dark) & (lr < mid)
    fill = float(lt[shadow].mean() / max(lr[shadow].mean(), 1e-6)) if shadow.any() else float("nan")
    midfill = float(lt[midm].mean() / max(lr[midm].mean(), 1e-6)) if midm.any() else float("nan")
    return {"slope": float(a), "intercept": float(b), "shadow_fill": fill, "mid_fill": midfill,
            "shadow_px_pct": 100.0 * float(shadow.mean())}


def main():
    vp = "atrium"
    pose = qb.VIEWPOINTS[vp]
    build = (ROOT / "build").resolve()
    exe = build / "Snowstorm-Runtime/Debug/Snowstorm-Runtime.exe"
    layer = (ROOT / "vcpkg" / "installed" / "x64-windows" / "bin").resolve()
    import tempfile
    tmp = Path(tempfile.gettempdir()) / "snowstorm-shadow-fill"
    tmp.mkdir(parents=True, exist_ok=True)

    ref, _, cached = qb.capture_reference(vp, pose, 400, exe, ROOT, 300, layer,
                                          qb.DEFAULT_SCENE, tmp)
    if ref is None:
        print("ref capture failed")
        return 1
    print(f"PT reference: {'cached' if cached else 'captured'} ({vp})\n")

    base = dict(qb.TECHNIQUES["all-rt"])
    # Isolation matrix: each row zeroes one indirect term on top of all-rt, so shadow_fill names the term
    # that over-fills shadows vs the PT. This is how #39 was pinned to the GI secondary-hit bounce ambient
    # (gi.intensity=0 collapsed the fill) and #163 to the env-cube spec. Add ad-hoc rows to sweep a knob.
    configs = {
        "all-rt (current defaults)": {},
        "spec_fade=0 (pre-#163)":    {"SS_RENDER_GI_SPEC_AMBIENT_FADE": "0"},
        "bounce_ambient=1 (pre-#39)":{"SS_RENDER_GI_BOUNCE_AMBIENT": "1"},
        "gi.intensity=0":            {"SS_RENDER_GI_INTENSITY": "0"},
        "ibl.intensity=0":           {"SS_RENDER_IBL_INTENSITY": "0"},
        "gi=0 AND ibl=0":            {"SS_RENDER_GI_INTENSITY": "0", "SS_RENDER_IBL_INTENSITY": "0"},
    }
    cam = qb.camera_env(pose)
    print(f"{'config':<28} {'slope':>7} {'intcpt':>8} {'shadowFill':>11} {'midFill':>8}")
    print("-" * 66)
    for name, ov in configs.items():
        img, _ = qb.run_capture({**base, **ov, **cam}, tmp / f"cfg", 60, exe, ROOT, 300, layer,
                                qb.DEFAULT_SCENE, max_frames=200)
        if img is None:
            print(f"{name:<28} capture FAILED")
            continue
        r = analyze(ref, img)
        print(f"{name:<28} {r['slope']:>7.3f} {r['intercept']:>8.3f} {r['shadow_fill']:>11.3f} {r['mid_fill']:>8.3f}")
    print("\nshadow_fill > 1 = real-time brighter than PT in PT-dark pixels; the config that drops it "
          "toward ~1 names the culprit term.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
