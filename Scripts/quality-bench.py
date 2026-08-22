#!/usr/bin/env python3
"""Image-quality gate: real-time techniques vs the path-traced reference (#153 increment 2).

The GPU perf-bench (Scripts/perf-bench.py) answers "how fast"; this answers "how correct".
For each viewpoint it captures the CONVERGED path tracer as ground truth, then each real-time
technique, and reports perceptual + numerical image-quality metrics (FLIP, PSNR, SSIM) of the
technique vs the reference, diffing against a committed baseline in Scripts/quality-baseline/
-- failing (exit 1) on a quality regression. This is how real-time GI/denoiser work is measured
in practice (NVIDIA FLIP; SVGF/ReSTIR papers report FLIP/SSIM vs a path-traced reference).

Both modes tonemap through the same LDR chain, so toggling render.pathtrace is an apples-to-apples
A/B: the engine's headless quality-capture (quality.capture.frames, phase A) dumps the final
present of each run to <path>_ldr.npy; this script loads the pair and computes the metrics offline.

Needs a real GPU (Vulkan + the path tracer), so it's a LOCAL gate like perf-bench.py, not CI.
Baselines are per-machine: the PT reference differs slightly across GPUs -- re-baseline on a new box.

Usage (from repo root or anywhere):
    py Scripts/quality-bench.py                    # capture ref + techniques, diff vs baseline
    py Scripts/quality-bench.py --update-baseline  # capture current metrics as the new baseline
    py Scripts/quality-bench.py --only ssao        # a single technique
    py Scripts/quality-bench.py --ref-frames 400   # PT accumulation frames for the reference (convergence)
    py Scripts/quality-bench.py --threshold 10     # regression tolerance %% (default 10)

FLIP is optional: if the `flip-evaluator` package isn't importable the run still gates on PSNR/SSIM
(install with `pip install flip-evaluator` to enable it). numpy is required.

Exit code: 0 if every technique is within threshold (or --update-baseline), 1 on a regression/failure.
"""
import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Pinned FLIP version: the perceptual metric must not drift between machines/runs or a committed baseline
# becomes meaningless (same reasoning as the clang-format / RGA pins).
FLIP_PIN = "flip-evaluator==1.7"

try:
    import numpy as np
except ImportError:
    print("FAIL: numpy is required (pip install numpy).")
    sys.exit(1)

DEFAULT_SCENE = "Projects/Sandbox/assets/scenes/Sponza.world"

# Viewpoints (#158). Captured in the RUNTIME (deterministic fixed viewport, no editor panels), pinned
# per viewpoint via the camera.override CVar (see camera_env). pose = {pos:[x,y,z], rot:[pitch,yaw,roll]
# radians}. All share a known-good position and vary orientation so none point into the void, while
# covering different content (atrium, floor, upper gallery) that stresses AO/GI/reflections differently.
# Averaging FLIP across these is what the auto-tuner (#161) minimizes, to avoid single-view overfit.
_SPONZA_POS = [8.519126892089844, 1.4949023723602295, -0.4308139383792877]
VIEWPOINTS = {
    "atrium":  {"pos": _SPONZA_POS, "rot": [0.027, 1.496, 0.0]},  # committed default: sunlit atrium down the nave
    "floor":   {"pos": _SPONZA_POS, "rot": [0.55, 1.496, 0.0]},   # tilt down: floor (AO/GI on the ground)
    "gallery": {"pos": _SPONZA_POS, "rot": [-0.5, 1.496, 0.0]},   # tilt up: upper gallery + sky (reflections/GI)
}
# Dropped two candidate orientations that rendered degenerate content from this spot (validated via the
# capture stats): yaw+pi faced a near-black wall (99.7% dark), and the side yaw was 78.7% dark / 0% bright.
# More/better viewpoints (from other positions) are trivial to add via camera.override.

# The path-traced reference: unbiased (clamps off), progressive -- --ref-frames controls convergence.
REF_ENV = {
    "SS_RENDER_PATHTRACE": "1",
    "SS_RENDER_PATHTRACE_CLAMP": "0",
    "SS_RENDER_PATHTRACE_WEIGHTCLAMP": "0",
}

# Technique name -> render-mode env overrides (SS_RENDER_*). Each is a full real-time render with that
# technique on; all use TAA (the realistic config the RT effects assume). Compared against the PT reference.
TECHNIQUES = {
    "raster": {"SS_RENDER_AA": "2"},  # baseline: shadow map, no AO/refl/GI
    "ssao": {"SS_RENDER_AO_MODE": "1", "SS_RENDER_AA": "2"},
    "rtao": {"SS_RENDER_AO_MODE": "2", "SS_RENDER_AA": "2"},
    "ssr": {"SS_RENDER_REFLECTIONS_MODE": "1", "SS_RENDER_AA": "2"},
    "rtrefl": {"SS_RENDER_REFLECTIONS_MODE": "2", "SS_RENDER_AA": "2"},
    "ssgi": {"SS_RENDER_GI_MODE": "1", "SS_RENDER_AA": "2"},
    "rtgi": {"SS_RENDER_GI_MODE": "2", "SS_RENDER_AA": "2"},
    "all-rt": {"SS_RENDER_SHADOWS_MODE": "2", "SS_RENDER_AO_MODE": "2",
               "SS_RENDER_REFLECTIONS_MODE": "2", "SS_RENDER_GI_MODE": "2", "SS_RENDER_AA": "2"},
}


# Canonical metric resolution. The capture size = the editor viewport = window minus panels, which is
# NOT deterministic across launches (observed 1177x649 and 1817x1009 in the same session), so raw captures
# can mismatch shape -> broken comparisons. Every capture is bilinear-resized to this fixed size before any
# metric, which (a) makes the gate deterministic across sessions/machines and (b) lets ref vs technique
# always compare 1:1. Chosen below both observed native sizes so it only ever downscales. The engine-side
# fixed-resolution render (#162) is the cleaner fix that avoids the resample; this is the metric-domain one.
CANON_W, CANON_H = 1024, 576


def _resize_bilinear(img: "np.ndarray", out_h: int, out_w: int) -> "np.ndarray":
    in_h, in_w = img.shape[:2]
    if (in_h, in_w) == (out_h, out_w):
        return img
    ys = np.clip((np.arange(out_h) + 0.5) * in_h / out_h - 0.5, 0, in_h - 1)
    xs = np.clip((np.arange(out_w) + 0.5) * in_w / out_w - 0.5, 0, in_w - 1)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, in_h - 1)
    x1 = np.minimum(x0 + 1, in_w - 1)
    wy = (ys - y0)[:, None, None]
    wx = (xs - x0)[None, :, None]
    top = img[y0][:, x0] * (1 - wx) + img[y0][:, x1] * wx
    bot = img[y1][:, x0] * (1 - wx) + img[y1][:, x1] * wx
    return top * (1 - wy) + bot * wy


# ---- metrics (offline, numpy) ------------------------------------------------------------------

def _luminance(rgb: "np.ndarray") -> "np.ndarray":
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def psnr(a: "np.ndarray", b: "np.ndarray") -> float:
    """PSNR over RGB, inputs in [0,255]. inf-clamped to 100 dB for identical images."""
    mse = float(np.mean((a[..., :3] - b[..., :3]) ** 2))
    if mse <= 1e-10:
        return 100.0
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def _box_mean(x: "np.ndarray", w: int) -> "np.ndarray":
    """Mean over a w x w window via an integral image (O(n), 'valid' shrink by w-1)."""
    c = np.cumsum(np.cumsum(x, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0)))
    s = c[w:, w:] - c[:-w, w:] - c[w:, :-w] + c[:-w, :-w]
    return s / float(w * w)


def ssim(a: "np.ndarray", b: "np.ndarray", w: int = 11) -> float:
    """Uniform-window SSIM on luminance (Wang et al.), inputs in [0,255]. skimage if available."""
    try:
        from skimage.metrics import structural_similarity
        return float(structural_similarity(a[..., :3], b[..., :3], channel_axis=2, data_range=255.0))
    except Exception:
        pass
    x = _luminance(a).astype(np.float64)
    y = _luminance(b).astype(np.float64)
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    mux, muy = _box_mean(x, w), _box_mean(y, w)
    mux2, muy2, muxy = _box_mean(x * x, w), _box_mean(y * y, w), _box_mean(x * y, w)
    vx, vy, cxy = mux2 - mux * mux, muy2 - muy * muy, muxy - mux * muy
    smap = ((2 * mux * muy + c1) * (2 * cxy + c2)) / ((mux * mux + muy * muy + c1) * (vx + vy + c2))
    return float(np.mean(smap))


_FLIP_STATE = {"tried": False, "fn": None}


def flip(a: "np.ndarray", b: "np.ndarray"):
    """Mean FLIP (LDR), lower = closer. Returns None if the flip-evaluator package isn't available."""
    if not _FLIP_STATE["tried"]:
        _FLIP_STATE["tried"] = True
        try:
            import flip_evaluator as fe
            _FLIP_STATE["fn"] = fe
        except Exception:
            # Auto-bootstrap the pinned FLIP (like rga-occupancy.py fetches RGA): install once, then retry.
            try:
                subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", FLIP_PIN], check=True)
                import flip_evaluator as fe
                _FLIP_STATE["fn"] = fe
            except Exception:
                print(f"  note: FLIP unavailable (pip install {FLIP_PIN} to enable) -- gating on PSNR/SSIM only.")
    fe = _FLIP_STATE["fn"]
    if fe is None:
        return None
    try:
        ref = np.ascontiguousarray(a[..., :3] / 255.0, dtype=np.float32)
        test = np.ascontiguousarray(b[..., :3] / 255.0, dtype=np.float32)
        _, mean_flip, _ = fe.evaluate(ref, test, "LDR")
        return float(mean_flip)
    except Exception as e:
        print(f"  note: FLIP evaluate failed ({e}) -- skipping FLIP.")
        _FLIP_STATE["fn"] = None
        return None


# ---- capture -----------------------------------------------------------------------------------

def run_capture(env_overrides: dict, out_base: Path, frames: int, exe: Path, cwd: Path,
                timeout: int, layer_path: Path, scene: str, max_frames: int = 0):
    """Run one headless capture; return (rgb_image[H,W,4] float, device_str) or (None, '').
    max_frames > 0 sets the hard capture cap: for the PT reference leave it 0 (converges via epsilon),
    but a real-time technique NEVER settles below the auto-stop epsilon (RT GI/AO/TAA keep a per-frame
    noise floor), so uncapped it burns the full 3000-frame safety cap (~100s/capture). Capping it at a
    small fixed value force-captures a deterministic settle window in ~7s -- and is more honest than
    3000 static frames, which over-accumulate TAA/RT beyond any real real-time frame."""
    ldr = out_base.with_name(out_base.name + "_ldr.npy")
    if ldr.exists():
        ldr.unlink()

    env = os.environ.copy()
    env["SS_QUALITY_CAPTURE_FRAMES"] = str(frames)
    if max_frames > 0:
        env["SS_QUALITY_CAPTURE_MAXFRAMES"] = str(max_frames)
    env["SS_QUALITY_CAPTURE_PATH"] = str(out_base)
    env["SS_STARTUP_SCENE"] = scene
    env["SS_VALIDATION_NONFATAL"] = "1"
    env["SS_CONFIG_IGNORE"] = "1"  # code defaults + these overrides only (baseline isolation)
    env.update(env_overrides)
    if layer_path and layer_path.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layer_path)

    # Retry transient failures. Rapid repeated launches occasionally flake (Vulkan/driver init, a lost
    # device, a missed readback) -> a one-off None would poison the tuner's objective as inf and wrongly
    # reject an otherwise-good config, so give each capture a couple of attempts before giving up.
    proc = None
    for attempt in range(3):
        if ldr.exists():
            ldr.unlink()
        try:
            proc = subprocess.run([str(exe)], cwd=str(cwd), env=env, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f"  FAIL (timed out after {timeout}s){' -- retrying' if attempt < 2 else ''}")
            continue
        if proc.returncode != 0:
            print(f"  FAIL (exit code {proc.returncode}){' -- retrying' if attempt < 2 else ''}")
            continue
        if not ldr.exists():
            print(f"  FAIL (no capture written to {ldr}){' -- retrying' if attempt < 2 else ''}")
            continue
        break
    else:
        return None, ""

    # "Selected GPU [i]: <name> (of N candidate(s))." is the unambiguous line (the candidate list logs
    # every adapter, so a vendor-keyword match can record one the run didn't use). Reduced to the bare
    # adapter name so it compares against perf-bench's device field. Keyword sweep is the fallback.
    device = ""
    for line in proc.stdout.splitlines():
        low = line.lower()
        if (m := re.search(r"Selected GPU \[\d+\]:\s*(.+?)\s*(?:\(of \d+|$)", line)):
            device = m.group(1).strip()[:64]
            break
        if not device and any(v in low for v in ("radeon", "geforce", "nvidia", "intel(r)", "arc ")):
            device = line.split("SNOWSTORM:")[-1].strip()[:64]
    # Normalize to the canonical metric resolution so window-size nondeterminism can't cause shape
    # mismatches / non-comparable metrics (see CANON_W/H).
    img = _resize_bilinear(np.load(ldr).astype(np.float64), CANON_H, CANON_W)
    return img, device


# The PT reference is deterministic given (scene, viewpoint, ref-frames, PT code, GPU), and the 400-frame
# accumulation is by far the most expensive capture. So cache it to disk keyed on a content hash of
# everything that changes the reference image; a subsequent run (another gate invocation, a tuner session)
# that only varies real-time CVars reuses the cached ground truth instead of re-accumulating it. The key
# includes the PT shader sources (recompiled at runtime, so they don't bump the exe) AND the runtime exe
# mtime (engine C++ PT path) AND the scene-file mtime, so any of those changing re-captures automatically.
# Known limitation: a material/mesh/texture edit that doesn't touch the .world file or rebuild the exe is
# NOT detected -- use --fresh-ref after such an edit. Cache dir is gitignored (per-machine, like baselines).
_REF_CACHE_VERSION = 1  # bump to invalidate all cached references on a format/keying change
_PT_SOURCES = ["Engine/Shaders/PathTrace.comp.hlsl", "Engine/Shaders/Include/Engine.hlsli"]


def _reference_key(repo_root: Path, exe: Path, scene: str, pose, ref_frames: int) -> str:
    h = hashlib.sha256()
    h.update(f"v{_REF_CACHE_VERSION}|{scene}|{ref_frames}|".encode())
    h.update(",".join(f"{v}" for v in (list(pose["pos"]) + list(pose["rot"]))).encode() if pose else b"none")
    for rel in _PT_SOURCES:
        p = repo_root / rel
        h.update(p.read_bytes() if p.exists() else b"missing")
    for p in (exe, repo_root / scene):
        h.update(str(p.stat().st_mtime_ns).encode() if p.exists() else b"0")
    return h.hexdigest()[:16]


def capture_reference(vp: str, pose, ref_frames: int, exe: Path, repo_root: Path, timeout: int,
                      layer_path: Path, scene: str, tmp: Path, fresh: bool = False):
    """Return the PT reference image [H,W,4], reusing a disk cache unless the key changed or `fresh`.
    Returns (img, device, cached_bool) or (None, '', False) on capture failure."""
    cache_dir = repo_root / "Scripts" / ".quality-ref-cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    key = _reference_key(repo_root, exe, scene, pose, ref_frames)
    cache_npy = cache_dir / f"{vp}__{key}.npy"

    if cache_npy.exists() and not fresh:
        try:
            return np.load(cache_npy), "", True
        except Exception as e:
            print(f"  note: cached reference unreadable ({e}); re-capturing.")

    img, dev = run_capture({**REF_ENV, **camera_env(pose)}, tmp / f"{vp}_ref", ref_frames, exe, repo_root,
                           timeout, layer_path, scene)
    if img is not None:
        np.save(cache_npy, img)
    return img, dev, False


def baseline_path(repo_root: Path, viewpoint: str, technique: str) -> Path:
    return repo_root / "Scripts" / "quality-baseline" / f"{viewpoint}__{technique}.json"


def camera_env(pose) -> dict:
    # Pin the runtime camera to this viewpoint via the camera.override CVar (RuntimeLayer applies it before
    # the first update, so the free-look controller seeds from it and holds). pose = {pos:[x,y,z],
    # rot:[pitch,yaw,roll] radians}; None = leave the scene's authored camera. Replaces the editor sidecar.
    if pose is None:
        return {}
    vals = list(pose["pos"]) + list(pose["rot"])
    return {"SS_CAMERA_OVERRIDE": ",".join(f"{v}" for v in vals)}


def regressed(metric: str, base: float, cur: float, threshold_pct: float) -> bool:
    """FLIP: higher is worse. PSNR/SSIM: lower is worse. Small dead-zone to swallow capture noise."""
    if base is None or cur is None:
        return False
    if metric == "flip":
        if base < 0.005 and cur < 0.005:
            return False
        return cur > base * (1.0 + threshold_pct / 100.0) + 1e-4
    # psnr / ssim: regression when the value DROPS by more than threshold%.
    return cur < base * (1.0 - threshold_pct / 100.0) - 1e-4


def main() -> int:
    ap = argparse.ArgumentParser(description="Image-quality gate vs the path-traced reference.")
    ap.add_argument("--frames", type=int, default=90, help="Frames per real-time technique capture (default 90)")
    ap.add_argument("--ref-frames", type=int, default=400, help="PT accumulation frames for the reference (default 400)")
    ap.add_argument("--timeout", type=int, default=300, help="Per-capture wall-clock timeout in seconds")
    ap.add_argument("--config", default="Debug", help="Build config dir under build/ (default Debug)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default build)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet for the validation-layer path")
    ap.add_argument("--only", default=None, help="Run only this technique (e.g. ssao, all-rt)")
    ap.add_argument("--threshold", type=float, default=10.0, help="Regression tolerance %% (default 10)")
    ap.add_argument("--scene", default=DEFAULT_SCENE, help="Scene to benchmark")
    ap.add_argument("--update-baseline", action="store_true", help="Write current metrics as the new baseline")
    ap.add_argument("--fresh-ref", action="store_true", help="Ignore the cached PT reference and re-capture it")
    ap.add_argument("--tech-maxframes", type=int, default=200, help="Hard frame cap for real-time technique captures "
                    "(they never converge below the auto-stop epsilon; uncapped they burn the full 3000-frame safety "
                    "cap ~100s each). Default 200 -> ~7s/capture. The PT reference is uncapped (converges).")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (repo_root / args.build_dir).resolve()
    layer_path = (repo_root / "vcpkg" / "installed" / args.triplet / "bin").resolve()
    exe = build_dir / f"Snowstorm-Runtime/{args.config}/Snowstorm-Runtime.exe"
    if not exe.exists():
        print(f"FAIL: executable not found at {exe} (build first, or check --config)")
        return 1

    techniques = TECHNIQUES
    if args.only:
        if args.only not in TECHNIQUES:
            print(f"No technique named '{args.only}'. Known: {list(TECHNIQUES)}")
            return 1
        techniques = {args.only: TECHNIQUES[args.only]}

    tmp = Path(tempfile.gettempdir()) / "snowstorm-quality-bench"
    tmp.mkdir(parents=True, exist_ok=True)
    (repo_root / "Scripts" / "quality-baseline").mkdir(parents=True, exist_ok=True)

    print(f"Repo root : {repo_root}")
    print(f"Build dir : {build_dir}  (config: {args.config})")
    print(f"Scene     : {args.scene}   Ref frames: {args.ref_frames}   Threshold: {args.threshold}%")
    print(f"Mode      : {'UPDATE BASELINE' if args.update_baseline else 'compare vs baseline'}\n")

    all_ok = True
    for vp, pose in VIEWPOINTS.items():
        cam = camera_env(pose)  # SS_CAMERA_OVERRIDE for this viewpoint (runtime); no scene/sidecar mutation
        ref_img, ref_dev, cached = capture_reference(vp, pose, args.ref_frames, exe, repo_root,
                                                     max(args.timeout, args.ref_frames // 2 + 60), layer_path,
                                                     args.scene, tmp, fresh=args.fresh_ref)
        src = "cached reference" if cached else f"captured path-traced reference ({args.ref_frames} frames)"
        print(f"=== viewpoint '{vp}': {src} ===")
        if ref_img is None:
            print("  reference capture FAILED; skipping viewpoint.\n")
            all_ok = False
            continue

        for tech, env in techniques.items():
            print(f"--- {vp} / {tech} ---")
            img, dev = run_capture({**env, **cam}, tmp / f"{vp}_{tech}", args.frames, exe, repo_root,
                                   args.timeout, layer_path, args.scene, max_frames=args.tech_maxframes)
            if img is None:
                all_ok = False
                continue
            if img.shape != ref_img.shape:
                print(f"  FAIL (size {img.shape} != reference {ref_img.shape})")
                all_ok = False
                continue

            cur = {"device": dev or ref_dev, "viewpoint": vp, "technique": tech,
                   "flip": flip(ref_img, img), "psnr": psnr(ref_img, img), "ssim": ssim(ref_img, img)}
            fl = f"{cur['flip']:.4f}" if cur["flip"] is not None else "n/a"
            print(f"  FLIP={fl}  PSNR={cur['psnr']:.2f}dB  SSIM={cur['ssim']:.4f}")

            bp = baseline_path(repo_root, vp, tech)
            if args.update_baseline:
                bp.write_text(json.dumps(cur, indent=2))
                print(f"  updated baseline: {bp.relative_to(repo_root)}")
            elif bp.exists():
                base = json.loads(bp.read_text())
                if base.get("device") and cur["device"] and base["device"] != cur["device"]:
                    print(f"  note: device differs (baseline '{base['device']}' vs current '{cur['device']}') "
                          f"-- the PT reference isn't comparable across GPUs; consider --update-baseline.")
                for m in ("flip", "psnr", "ssim"):
                    b, c = base.get(m), cur.get(m)
                    if regressed(m, b, c, args.threshold):
                        print(f"  REGRESSION [{m}]: baseline {b} -> current {c}")
                        all_ok = False
            else:
                print(f"  no baseline at {bp.relative_to(repo_root)} -- run with --update-baseline first.")

    print("\n=== Summary ===")
    print("PASS" if all_ok else "FAIL (regression or run failure)")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
