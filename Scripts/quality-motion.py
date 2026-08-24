#!/usr/bin/env python3
"""Image-quality gate UNDER CAMERA MOTION, against a per-frame path-traced reference.

quality-bench.py measures a static viewpoint, which is the wrong test for anything temporal: TAA,
the SVGF-style denoisers and the stochastic shadow pass all exist to exploit motion, and all of
their characteristic failures (ghosting, disocclusion trails, history rejection, boiling) only
appear while the camera moves. This gate flies the committed camera route and measures the frames
it renders along the way.

PROTOCOL (this is BMFR's, Koskela et al. TOG 2019: an animated sequence with a converged per-frame
reference, metrics averaged over the sequence):

  1. One headless Runtime run per technique, flying camera.path with quality.capture.at_path_frames
     set, which writes the requested route frames plus a manifest of the pose each was taken at.
  2. Per captured frame, a SEPARATE path trace of the world REPLAYED to that frame and held there
     (sim.freeze_frame) is the ground truth. It has to be separate: the path tracer resets
     accumulation on any view-projection change, so a moving path trace is a 1-sample noise image
     that never converges. It has to freeze the whole world rather than just pin the camera, or
     animated props keep spinning through the accumulation and converge to a smear.
  3. Per-frame FLIP/PSNR/SSIM against that reference, plus a temporal term over frame pairs.

Frames are requested in ADJACENT PAIRS. A temporal metric needs consecutive frames, and averaging
per-frame FLIP over a sequence provably cannot separate stable distortion from flicker: two
techniques with identical mean spatial error rank the same whether one is steady and the other
strobes.

Usage:
    py Scripts/quality-motion.py                    # all techniques, diff vs baseline
    py Scripts/quality-motion.py --update-baseline  # capture current metrics as the new baseline
    py Scripts/quality-motion.py --only rtgi        # one technique
    py Scripts/quality-motion.py --fresh-ref        # ignore cached PT references
    py Scripts/quality-motion.py --probes dolly,strafe

Exit: 0 within threshold (or --update-baseline), 1 on a regression or run failure, 2 when nothing
was compared (no baseline for this adapter, references unavailable). Exit 2 is not a pass.

Needs a real GPU (Vulkan + the path tracer), so this is a LOCAL gate, not CI. Baselines are keyed by
adapter for the same reason quality-bench's are: the reference is a path trace on the local card.
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

# Reuse the sibling gate's capture plumbing and metrics rather than forking them: the two must agree
# on resize, FLIP, device slug and baseline layout or their numbers stop being comparable. Loaded by
# path because the filename is not an identifier (quality-tune.py does the same).
_QB_PATH = Path(__file__).resolve().parent / "quality-bench.py"
_spec = importlib.util.spec_from_file_location("quality_bench", _QB_PATH)
qb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qb)

ROUTE = "Projects/Sandbox/assets/camera-paths/sponza-bench.json"

# Sponza plus animated props. The static gates keep using plain Sponza.world, deliberately: adding
# moving geometry there would invalidate every committed quality-bench and perf baseline, and the
# props exist for a stressor those gates cannot measure anyway. A camera-only flythrough of static
# geometry produces no OBJECT disocclusion, and camera-relative and object-relative motion are not
# equivalent tests: a moving object reveals background against history the camera never disturbed.
MOTION_SCENE = "Projects/Sandbox/assets/scenes/Sponza-Motion.world"

# Probe points along the committed route, each an ADJACENT frame pair. Frame indices are route-local
# (see CameraPathComponent::Frame) and map to the route's phases, which is why they are named rather
# than evenly spaced: each phase breaks temporal reconstruction a different way.
#
#   route: 27.82 units at 2.0 u/s = 835 frames, waypoints at frames 0/123/228/348/454/501/609/729/835
#
#   dolly     forward translation. Mostly measures history LENGTH; little disocclusion.
#   strafe    lateral motion past the colonnade, so columns sweep across view. The canonical
#             disocclusion generator: revealed background reprojects onto history left by the
#             occluder, so the reprojection test passes while the data is wrong.
#   reversal  the U-turn. Rapid direction change is a documented pathological case for history
#             rejection (FSR2 calls out high-velocity ghosting as its own category).
#   static    past the end of the open route, where it parks. Convergence once motion STOPS is its
#             own failure mode, and both BMFR and QRISP keep a stationary control for it.
PROBES = {
    "dolly": (60, 61),
    "strafe": (300, 301),
    "reversal": (500, 501),
    "static": (900, 901),
}

MOTION_ENV = {
    "SS_CAMERA_PATH": "1",
    "SS_CAMERA_PATH_FILE": ROUTE,
    "SS_CAMERA_PATH_FIXED": "1",  # fixed 60 Hz step; wall-clock would desync pose from frame index
}


def probe_frames(names) -> list:
    out = []
    for n in names:
        out.extend(PROBES[n])
    return sorted(out)


def run_motion_capture(tech_env: dict, out_base: Path, exe: Path, repo_root: Path, timeout: int,
                       layer_path: Path, scene: str, frames: list, tech_maxframes: int):
    """Fly the route once, returning ({frame: image}, {frame: pose}, device).

    Images come back at the canonical metric resolution, like quality-bench's, so the two gates'
    numbers stay comparable. ({}, {}, RT_UNSUPPORTED) when the adapter cannot run the technique.
    """
    for stale in out_base.parent.glob(out_base.name + "_f*_ldr.npy"):
        stale.unlink()
    manifest_path = out_base.with_name(out_base.name + "_poses.json")
    manifest_path.unlink(missing_ok=True)

    env = os.environ.copy()
    env.update({
        "SS_QUALITY_CAPTURE_FRAMES": "1",  # unused in sequence mode, but must be > 0 to arm the capture
        "SS_QUALITY_CAPTURE_AT_PATH_FRAMES": ",".join(str(f) for f in frames),
        "SS_QUALITY_CAPTURE_PATH": str(out_base),
        "SS_QUALITY_CAPTURE_MAXFRAMES": str(tech_maxframes),
        "SS_STARTUP_SCENE": scene,
        "SS_VALIDATION_NONFATAL": "1",
        "SS_CONFIG_IGNORE": "1",
    })
    env.update(MOTION_ENV)
    env.update(tech_env)
    if layer_path and layer_path.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layer_path)

    proc = None
    for attempt in range(3):
        try:
            proc = subprocess.run([str(exe)], cwd=str(repo_root), env=env, capture_output=True,
                                  text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f"  FAIL (timed out after {timeout}s){' -- retrying' if attempt < 2 else ''}")
            continue
        if "Quality capture: ray tracing unsupported" in proc.stdout:
            print("  SKIP (ray tracing unsupported on this adapter; the technique never ran)")
            return {}, {}, qb.RT_UNSUPPORTED
        if proc.returncode != 0:
            print(f"  FAIL (exit code {proc.returncode}){' -- retrying' if attempt < 2 else ''}")
            continue
        if not manifest_path.exists():
            print(f"  FAIL (no manifest at {manifest_path}){' -- retrying' if attempt < 2 else ''}")
            continue
        break
    else:
        return {}, {}, ""

    device = ""
    for line in proc.stdout.splitlines():
        if (m := qb.re.search(r"Selected GPU \[\d+\]:\s*(.+?)\s*(?:\(of \d+|$)", line)):
            device = m.group(1).strip()[:64]
            break

    imgs, poses = {}, {}
    for entry in json.loads(manifest_path.read_text())["frames"]:
        f = int(entry["frame"])
        npy = out_base.with_name(f"{out_base.name}_f{f:06d}_ldr.npy")
        if not npy.exists():
            print(f"  FAIL (manifest lists frame {f} but {npy.name} is missing)")
            return {}, {}, ""
        imgs[f] = qb._resize_bilinear(np.load(npy).astype(np.float64), qb.CANON_H, qb.CANON_W)
        poses[f] = {"pos": entry["camera"][:3], "rot": entry["camera"][3:]}

    missing = [f for f in frames if f not in imgs]
    if missing:
        print(f"  FAIL (route frames never captured: {missing})")
        return {}, {}, ""
    return imgs, poses, device


_REF_CACHE_VERSION = 2  # bump to invalidate every cached motion reference


def _motion_reference_key(repo_root: Path, exe: Path, scene: str, frame: int, ref_frames: int) -> str:
    """Cache key for a frozen-frame reference.

    Keyed on the ROUTE FRAME, never on the camera pose. With animated props those are not the same
    thing: the route parks at its end, so frames 900 and 901 share a pose while the props sit at
    different angles, and a pose-keyed cache would serve one as the other.
    """
    h = qb.hashlib.sha256()
    h.update(f"v{_REF_CACHE_VERSION}|{scene}|{frame}|{ref_frames}|".encode())
    for rel in [ROUTE, *qb._PT_SOURCES]:
        p = repo_root / rel
        h.update(p.read_bytes() if p.exists() else b"missing")
    for p in (exe, repo_root / scene):
        h.update(str(p.stat().st_mtime_ns).encode() if p.exists() else b"0")
    return h.hexdigest()[:16]


def capture_motion_reference(frame: int, ref_frames: int, exe: Path, repo_root: Path, timeout: int,
                             layer_path: Path, scene: str, tmp: Path, fresh: bool = False):
    """Converged path trace of the world REPLAYED TO route frame `frame` and held there.

    Not a static capture at the frame's camera pose: that pins the camera and leaves animated props
    spinning through the accumulation, so the path tracer would converge on a smear of every angle
    they passed through. sim.freeze_frame advances camera and props together and stops both, which is
    the only state that is actually ground truth for the moving frame it is paired with.

    Returns (image, device, cached) with image None on failure.
    """
    cache_dir = repo_root / "Scripts" / ".quality-ref-cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    key = _motion_reference_key(repo_root, exe, scene, frame, ref_frames)
    cache_npy = cache_dir / f"motion-f{frame:06d}__{key}.npy"

    if cache_npy.exists() and not fresh:
        try:
            return np.load(cache_npy), "", True
        except Exception as e:
            print(f"  note: cached reference unreadable ({e}); re-capturing.")

    env = {**qb.REF_ENV, **MOTION_ENV, "SS_SIM_FREEZE_FRAME": str(frame)}
    img, dev = qb.run_capture(env, tmp / f"ref_f{frame:06d}", ref_frames, exe, repo_root, timeout,
                              layer_path, scene)
    if img is not None:
        np.save(cache_npy, img)
    return img, dev, False


def capture_static_controls(tech_env: dict, out_base: Path, exe: Path, repo_root: Path, timeout: int,
                            layer_path: Path, scene: str, probe_names) -> dict:
    """Render each probe's pose again with the camera PARKED, same technique. Returns {probe: image}.

    This is the control that separates motion cost from content difficulty, and it is not optional:
    the probes look at different parts of Sponza, so an absolute score difference between two probes
    is mostly a difference in what is on screen. Subtracting the same viewpoint held still leaves the
    part attributable to motion.

    Frozen with sim.freeze_frame, the same way the reference is, so all three images describe one
    world state: the reference path-traces it, the control renders it still, and the moving capture
    renders it in motion. Pinning only the camera would leave the props spinning in the control, and
    the penalty would then measure camera motion against a partially-moving baseline.

    The still side lets TAA accumulate longer than a moving frame ever gets. That is the definition
    of the measurement (cost versus everything having stopped), not a confound, and it is
    empirically small: the `static` probe, already parked in the moving run, scores within 0.001
    FLIP of its own control on every technique.
    """
    out = {}
    for name in probe_names:
        f0 = PROBES[name][0]
        # The frame cap has to clear the freeze point: reaching route frame N means actually RENDERING
        # N frames first, so a cap below it captures the world mid-route instead of frozen. Getting this
        # wrong inverts the metric, since the control then sits further from the reference than the
        # moving capture does and the penalty comes out negative.
        img, _dev = qb.run_capture({**tech_env, **MOTION_ENV, "SS_SIM_FREEZE_FRAME": str(f0)},
                                   out_base.with_name(f"{out_base.name}_static_{name}"),
                                   30, exe, repo_root, timeout, layer_path, scene, max_frames=f0 + 400)
        if img is None:
            print(f"  static control capture failed for probe '{name}'.")
            return {}
        out[name] = img
    return out


def temporal_flip(g0, g1, r0, r1):
    """Temporal error over one adjacent pair. Lower is better; None if FLIP is unavailable.

    This is tLP's construction (Chu et al., TecoGAN, TOG 2020) with FLIP substituted for LPIPS:
    compare how much the TECHNIQUE changed between consecutive frames against how much the
    REFERENCE changed over the same interval. The subtraction is the whole point. A raw
    frame-to-frame difference punishes legitimate motion, so it would rank a blurrier, laggier
    result as more "stable"; measuring against the reference's own rate of change does not.

    Substituting FLIP is a deliberate deviation from the published metric, which uses LPIPS. FLIP is
    already this repo's pinned perceptual metric and needs no torch, and both are perceptual
    distances used the same way here. Report it as tFLIP, never as tLP.
    """
    a = qb.flip(g0, g1)
    b = qb.flip(r0, r1)
    if a is None or b is None:
        return None
    return abs(a - b)


_CVVDP_STATE = {"tried": False, "fn": None}


CVVDP_INSTALL = "pip install git+https://github.com/gfxdisp/ColorVideoVDP.git"


def colorvideovdp(imgs, refs, probe_names, fps: float = 60.0):
    """ColorVideoVDP JOD per probe (higher is better, 10 = indistinguishable). Returns (mean, per-probe).

    Mantiuk et al., TOG 2024: a spatio-temporal difference predictor that models temporal contrast
    sensitivity, so it predicts flicker and judder directly rather than inferring them from a spatial
    score. Reported but NOT gated, and optional (it needs torch, which the other gates do not).

    Evaluated PER PROBE PAIR, never over all captured frames at once. The probes are far apart on the
    route, so concatenating them hands the metric three enormous scene cuts and it reads them as real
    temporal content: measured, that inflates the result by 0.627 JOD on all-rt, and the paper's own
    scale puts 1 JOD at roughly a 75% population preference. Two frames is thin temporal context, but
    thin and honest beats thick and fabricated.
    """
    if not _CVVDP_STATE["tried"]:
        _CVVDP_STATE["tried"] = True
        try:
            import pycvvdp
            _CVVDP_STATE["fn"] = pycvvdp
        except Exception:
            print(f"  note: ColorVideoVDP unavailable ({CVVDP_INSTALL}); reporting without it.")
    mod = _CVVDP_STATE["fn"]
    if mod is None:
        return None, {}
    try:
        metric = mod.cvvdp(display_name="standard_4k", heatmap=None)
        per = {}
        for name in probe_names:
            pair = PROBES[name]
            test = np.stack([imgs[f][..., :3].astype(np.uint8) for f in pair])
            ref = np.stack([refs[f][..., :3].astype(np.uint8) for f in pair])
            jod, _ = metric.predict(test, ref, dim_order="FHWC", frames_per_second=fps)
            per[name] = float(jod)
        return (float(np.mean(list(per.values()))) if per else None), per
    except Exception as e:
        print(f"  note: ColorVideoVDP failed ({e}); reporting without it.")
        _CVVDP_STATE["fn"] = None
        return None, {}


def evaluate(imgs, refs, probe_names, statics=None):
    """Per-frame spatial metrics, the per-pair temporal term, and the motion penalty."""
    per_frame, per_pair = [], []
    for name in probe_names:
        f0, f1 = PROBES[name]
        for f in (f0, f1):
            per_frame.append({
                "probe": name, "frame": f,
                "flip": qb.flip(refs[f], imgs[f]),
                "psnr": qb.psnr(refs[f], imgs[f]),
                "ssim": qb.ssim(refs[f], imgs[f]),
            })
        pen = None
        if statics and name in statics:
            moving = qb.flip(refs[f0], imgs[f0])
            parked = qb.flip(refs[f0], statics[name])
            pen = (moving - parked) if (moving is not None and parked is not None) else None
        per_pair.append({"probe": name,
                         "tflip": temporal_flip(imgs[f0], imgs[f1], refs[f0], refs[f1]),
                         "motionPenalty": pen})

    def mean_of(rows, key):
        vals = [r[key] for r in rows if r[key] is not None]
        return float(np.mean(vals)) if vals else None

    return {
        "flip": mean_of(per_frame, "flip"),
        "psnr": mean_of(per_frame, "psnr"),
        "ssim": mean_of(per_frame, "ssim"),
        "tflip": mean_of(per_pair, "tflip"),
        "motionPenalty": mean_of(per_pair, "motionPenalty"),
        "perFrame": per_frame,
        "perPair": per_pair,
    }


def baseline_path(repo_root: Path, device: str, technique: str) -> Path:
    return repo_root / "Scripts" / "quality-motion-baseline" / qb.device_slug(device) / f"{technique}.json"


def main() -> int:
    ap = argparse.ArgumentParser(description="Image-quality gate under camera motion.")
    ap.add_argument("--ref-frames", type=int, default=400, help="PT settle window per reference (default 400)")
    ap.add_argument("--timeout", type=int, default=600, help="Per-capture wall-clock timeout in seconds")
    ap.add_argument("--config", default="Debug", help="Build config dir under build/ (default Debug)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default build)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet for the validation-layer path")
    ap.add_argument("--only", default=None, help="Run only this technique (e.g. rtgi, all-rt)")
    ap.add_argument("--probes", default=",".join(PROBES), help="Comma-separated probe names")
    ap.add_argument("--threshold", type=float, default=10.0, help="Regression tolerance %% (default 10)")
    ap.add_argument("--scene", default=MOTION_SCENE, help="Scene to benchmark")
    ap.add_argument("--tech-maxframes", type=int, default=4000,
                    help="Hard frame cap per motion capture; must exceed the last probe frame")
    ap.add_argument("--update-baseline", action="store_true", help="Write current metrics as the new baseline")
    ap.add_argument("--fresh-ref", action="store_true", help="Ignore cached PT references and re-capture")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (repo_root / args.build_dir).resolve()
    layer_path = (repo_root / "vcpkg" / "installed" / args.triplet / "bin").resolve()
    exe = build_dir / f"Snowstorm-Runtime/{args.config}/Snowstorm-Runtime.exe"
    tmp = repo_root / "Scripts" / ".quality-motion-tmp"
    tmp.mkdir(parents=True, exist_ok=True)

    if not exe.exists():
        print(f"FAIL: executable not found at {exe} (build first, or check --config)")
        return 1
    if not (repo_root / ROUTE).exists():
        print(f"FAIL: camera route missing at {ROUTE}")
        return 1

    probe_names = [p.strip() for p in args.probes.split(",") if p.strip()]
    if unknown := [p for p in probe_names if p not in PROBES]:
        print(f"Unknown probe(s) {unknown}. Known: {list(PROBES)}")
        return 1
    frames = probe_frames(probe_names)

    techniques = qb.TECHNIQUES
    if args.only:
        if args.only not in techniques:
            print(f"No technique named '{args.only}'. Known: {list(techniques)}")
            return 1
        techniques = {args.only: techniques[args.only]}

    print(f"Repo root : {repo_root}")
    print(f"Route     : {ROUTE}")
    print(f"Scene     : {args.scene}")
    print(f"Probes    : {', '.join(probe_names)}  -> route frames {frames}")
    print(f"Mode      : {'UPDATE BASELINE' if args.update_baseline else 'compare vs baseline'}\n")

    all_ok, ungated, ran_any = True, [], False
    ref_cache, ref_poses = {}, None

    for tech, tech_env in techniques.items():
        print(f"=== {tech} ===")
        imgs, poses, device = run_motion_capture(tech_env, tmp / f"motion_{tech}", exe, repo_root,
                                                 args.timeout, layer_path, args.scene, frames,
                                                 args.tech_maxframes)
        if device == qb.RT_UNSUPPORTED:
            ungated.append(tech)
            print()
            continue
        if not imgs:
            all_ok = False
            print()
            continue

        # Every technique must fly the identical route, or its frames and the cached references are
        # different viewpoints and the comparison is meaningless. This is the run-to-run determinism
        # the whole design rests on, so check it every time rather than trusting it.
        if ref_poses is None:
            ref_poses = poses
        else:
            for f in frames:
                if any(abs(a - b) > 1e-4 for a, b in zip(poses[f]["pos"] + poses[f]["rot"],
                                                         ref_poses[f]["pos"] + ref_poses[f]["rot"])):
                    print(f"  FAIL: route frame {f} was at a different pose than the first technique's run.")
                    print(f"        {poses[f]} vs {ref_poses[f]}")
                    print("        The route is not reproducing; every metric below would be comparing viewpoints.")
                    return 1

        # Ground truth per captured frame: a static path trace pinned to that frame's pose. Cached on
        # disk by pose, so it is paid once and shared across every technique.
        missing_ref = False
        for f in frames:
            if f in ref_cache:
                continue
            print(f"  reference for route frame {f} ...")
            img, dev, cached = capture_motion_reference(f, args.ref_frames, exe, repo_root, args.timeout,
                                                        layer_path, args.scene, tmp, args.fresh_ref)
            if img is None:
                print(f"  reference capture failed for frame {f}.")
                missing_ref = True
                break
            if dev and not device:
                device = dev
            ref_cache[f] = img
            print(f"    {'cached' if cached else 'captured'}")
        if missing_ref:
            all_ok = False
            print()
            continue

        statics = capture_static_controls(tech_env, tmp / f"motion_{tech}", exe, repo_root,
                                          args.timeout, layer_path, args.scene, probe_names)
        result = evaluate(imgs, ref_cache, probe_names, statics)
        result["cvvdpJod"], result["cvvdpPerProbe"] = colorvideovdp(imgs, ref_cache, probe_names)
        ran_any = True

        for row in result["perFrame"]:
            print(f"    {row['probe']:<9} f{row['frame']:<5} FLIP {fmt(row['flip'])}  "
                  f"PSNR {fmt(row['psnr'])}  SSIM {fmt(row['ssim'])}")
        for row in result["perPair"]:
            jod = result["cvvdpPerProbe"].get(row["probe"])
            print(f"    {row['probe']:<9} pair  tFLIP {fmt(row['tflip'])}  "
                  f"motionPen {fmt(row['motionPenalty'])}"
                  + (f"  cvvdp {jod:6.3f} JOD" if jod is not None else ""))
        print(f"  MEAN  FLIP {fmt(result['flip'])}  PSNR {fmt(result['psnr'])}  "
              f"SSIM {fmt(result['ssim'])}  tFLIP {fmt(result['tflip'])}  "
              f"motionPen {fmt(result['motionPenalty'])}"
              + (f"  cvvdp {fmt(result['cvvdpJod'])} JOD" if result["cvvdpJod"] is not None else ""))

        bp = baseline_path(repo_root, device, tech)
        record = {"device": device, "technique": tech, "route": ROUTE, "probes": probe_names,
                  "flip": result["flip"], "psnr": result["psnr"], "ssim": result["ssim"],
                  "tflip": result["tflip"], "motionPenalty": result["motionPenalty"],
                  "cvvdpJod": result["cvvdpJod"],
                  "cvvdpPerProbe": result["cvvdpPerProbe"],
                  "perFrame": result["perFrame"], "perPair": result["perPair"]}
        if args.update_baseline:
            bp.parent.mkdir(parents=True, exist_ok=True)
            bp.write_text(json.dumps(record, indent=2))
            print(f"  updated baseline: {bp.relative_to(repo_root)}  (device: {device or '?'})")
        elif bp.exists():
            base = json.loads(bp.read_text())
            if base.get("device") and device and base["device"] != device:
                ungated.append(tech)
                print(f"  NOT GATED: baseline was captured on '{base['device']}', this run is '{device}'.")
            elif base.get("probes") and base["probes"] != probe_names:
                # Every metric here is a mean over the probe set, so a different set is a different
                # number, not a regression. Comparing them would flag route phases nobody changed.
                ungated.append(tech)
                print(f"  NOT GATED: baseline covers probes {base['probes']}, this run {probe_names}.")
            elif base.get("route") and base["route"] != ROUTE:
                ungated.append(tech)
                print(f"  NOT GATED: baseline flew '{base['route']}', this run '{ROUTE}'.")
            else:
                for metric in ("flip", "psnr", "ssim", "tflip", "motionPenalty"):
                    if qb.regressed("flip" if metric in ("tflip", "motionPenalty") else metric,
                                    base.get(metric), result[metric], args.threshold):
                        print(f"  REGRESSION in {metric}: {fmt(base.get(metric))} -> {fmt(result[metric])}")
                        all_ok = False
        else:
            ungated.append(tech)
            print(f"  NOT GATED: no baseline at {bp.relative_to(repo_root)} for '{device or 'unknown device'}'.")
        print()

    print("=== Summary ===")
    if not all_ok:
        print("FAIL (regression or run failure)")
        return 1
    if ungated or not ran_any:
        print(f"SKIP: {len(ungated)} technique(s) never compared: {', '.join(ungated) or '(none ran)'}")
        print("      nothing was compared for those, so this run is not a pass.")
        return 2
    print("PASS")
    return 0


def fmt(v) -> str:
    return "  n/a " if v is None else f"{v:6.4f}"


if __name__ == "__main__":
    sys.exit(main())
