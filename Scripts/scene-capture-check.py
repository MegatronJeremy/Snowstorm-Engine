#!/usr/bin/env python3
"""Does the scene LOOK right? Captures each smoke and showcase scene headlessly and gates two things.

smoke-test.py proves a scene boots, runs and settles without error markers. It cannot see a frame that is
all magenta or all sky, because nothing in the log says so. This gate looks at the pixels:

  1. Error-frame detector (no golden needed): a frame dominated by one colour, by magenta, or by black
     fails. That is what a missing shader, an unresolved texture or a scene that never loaded draws.
  2. Golden compare: the capture against a committed golden of the same scene on the same adapter,
     measured with quality-bench's FLIP (PSNR when FLIP is not installed). Past --threshold fails.

Goldens are keyed by adapter like every other baseline: Scripts/scene-golden/<device-slug>/<scene>.npz
(the capture downscaled to 512x288, uint8). A missing golden is a SKIP (exit 2), not a pass, but the error
detector still runs and still fails.

    py Scripts/scene-capture-check.py                      # capture all, detect error frames, diff goldens
    py Scripts/scene-capture-check.py --only Reflections
    py Scripts/scene-capture-check.py --update-golden      # after LOOKING at the PNGs in Scripts/.scene-captures
    py Scripts/scene-capture-check.py --from-exe-dir       # launch the way a double-click does

Every capture is also written as a PNG to Scripts/.scene-captures/<scene>.png: attach before/after PNGs to a
render PR, and use a real capture (not a debug buffer) on a showcase page.

Exit 0 pass, 1 error frame or golden mismatch or failed capture, 2 nothing compared (no golden, no capture).
"""
import argparse
import importlib.util
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

GOLDEN_W, GOLDEN_H = 512, 288
DEFAULT_THRESHOLD = 0.10  # mean FLIP; the quality gate's techniques sit 0.03-0.15 from the path tracer

# (name, exe relative to the build dir, scene relative to the repo root or None for the project's own).
SCENES = [
    ("Sponza", "Snowstorm-Runtime/{config}/Snowstorm-Runtime.exe", "Projects/Sandbox/assets/scenes/Sponza.world", {}),
    ("Reflections", "Snowstorm-Runtime/{config}/Snowstorm-Runtime.exe", "Projects/Sandbox/assets/scenes/Reflections.world", {}),
    ("AlphaBlend", "Snowstorm-Runtime/{config}/Snowstorm-Runtime.exe", "Projects/Sandbox/assets/scenes/AlphaBlend.world",
     {"SS_RENDER_TRANSPARENT": "1"}),
    ("Pong", "Games/Pong/{config}/Snowstorm-Pong.exe", "Projects/Pong/assets/scenes/Pong.world", {}),
]

# Error-frame rules. Thresholds are deliberately loose: a real scene with a large flat sky or a dark corridor
# stays far below them, while the failure they target (the whole frame one colour) sits at 0.95+.
DOMINANT_COLOUR_FRACTION = 0.90   # one 4-bit-per-channel colour bin covers this much of the frame
MAGENTA_FRACTION = 0.25           # r,b high and g low: the classic missing-texture / error colour
BLACK_FRACTION = 0.90             # every channel near zero


def detect_error_frame(img):
    """Return a reason string when `img` (HxWx3+ uint8 or float 0..255) looks like an error frame, else None."""
    rgb = np.asarray(img)[..., :3].astype(np.float64)
    if rgb.max() <= 1.0:
        rgb = rgb * 255.0
    n = rgb.shape[0] * rgb.shape[1]
    px = rgb.reshape(-1, 3)

    black = float(np.mean(px.max(axis=1) < 8.0))
    if black >= BLACK_FRACTION:
        return f"{black:.0%} of the frame is black"

    magenta = float(np.mean((px[:, 0] > 200) & (px[:, 2] > 200) & (px[:, 1] < 80)))
    if magenta >= MAGENTA_FRACTION:
        return f"{magenta:.0%} of the frame is magenta"

    bins = (px.astype(np.int64) >> 4)
    keys = bins[:, 0] * 256 + bins[:, 1] * 16 + bins[:, 2]
    top = float(np.bincount(keys).max()) / n
    if top >= DOMINANT_COLOUR_FRACTION:
        return f"{top:.0%} of the frame is a single colour"
    return None


def write_png(path, img):
    """Minimal RGB PNG writer (numpy + zlib only)."""
    rgb = np.clip(np.asarray(img)[..., :3], 0, 255).astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def _load_qb(scripts_dir):
    spec = importlib.util.spec_from_file_location("quality_bench", scripts_dir / "quality-bench.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", default=None, help="One scene by name")
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--triplet", default="x64-windows")
    ap.add_argument("--frames", type=int, default=120, help="Capture this many frames after steady state (default 120)")
    ap.add_argument("--timeout", type=int, default=480)
    ap.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD, help="Max mean FLIP against the golden")
    ap.add_argument("--update-golden", action="store_true", help="Write the captures as the new goldens (look at the PNGs first)")
    ap.add_argument("--from-exe-dir", action="store_true", help="Run with the working directory set to the exe's own folder, as a double-click does")
    args = ap.parse_args()

    scripts_dir = Path(__file__).resolve().parent
    repo_root = scripts_dir.parent
    build_dir = repo_root / args.build_dir
    qb = _load_qb(scripts_dir)
    layer_path = repo_root / "vcpkg" / "installed" / args.triplet / "bin"
    out_dir = scripts_dir / ".scene-captures"
    out_dir.mkdir(exist_ok=True)

    scenes = [s for s in SCENES if not args.only or s[0].lower() == args.only.lower()]
    if not scenes:
        print(f"No scene named '{args.only}'. Known: {[s[0] for s in SCENES]}")
        return 1

    failed, compared, skipped = [], 0, []
    for name, rel, scene, extra in scenes:
        exe = build_dir / rel.format(config=args.config)
        print(f"[{name}] {scene}")
        if not exe.exists():
            print(f"  FAIL: {exe} not built")
            failed.append(name)
            continue
        cwd = exe.parent if args.from_exe_dir else repo_root
        img, device = qb.run_capture(extra, out_dir / name, 0, exe, cwd, args.timeout, layer_path, scene,
                                     max_frames=args.frames)
        if img is None:
            print("  FAIL: no capture")
            failed.append(name)
            continue
        img = np.clip(img, 0, 255)
        small = qb._resize_bilinear(img, GOLDEN_H, GOLDEN_W)
        write_png(out_dir / f"{name}.png", small)

        reason = detect_error_frame(small)
        if reason:
            print(f"  FAIL: error frame, {reason}  (see {out_dir / (name + '.png')})")
            failed.append(name)
            continue

        golden = scripts_dir / "scene-golden" / qb.device_slug(device) / f"{name}.npz"
        if args.update_golden:
            golden.parent.mkdir(parents=True, exist_ok=True)
            np.savez_compressed(golden, rgb=np.round(small[..., :3]).astype(np.uint8))
            print(f"  golden written: {golden}")
            continue
        if not golden.exists():
            print(f"  SKIP golden compare: no golden for adapter '{device}' ({golden})")
            skipped.append(name)
            continue
        ref = np.load(golden)["rgb"].astype(np.float64)
        compared += 1
        f = qb.flip(ref, small[..., :3])
        if f is not None:
            ok = f <= args.threshold
            print(f"  FLIP vs golden {f:.4f} (max {args.threshold}) {'ok' if ok else 'FAIL'}")
        else:
            p = qb.psnr(ref, small[..., :3])
            ok = p >= 20.0
            print(f"  PSNR vs golden {p:.2f} dB (min 20, FLIP unavailable) {'ok' if ok else 'FAIL'}")
        if not ok:
            failed.append(name)

    print("\n=== Summary ===")
    if failed:
        print(f"  FAIL: {failed}")
        return 1
    if args.update_golden:
        return 0
    if compared == 0:
        print(f"  SKIP: nothing compared to a golden (no golden for {skipped}); error-frame check passed")
        return 2
    print(f"  PASS: {compared} compared, {len(skipped)} without a golden")
    return 0


if __name__ == "__main__":
    sys.exit(main())
