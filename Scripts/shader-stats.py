"""Driver-reported per-shader statistics, cross-vendor.

The occupancy gate (Scripts/rga-occupancy.py) is the Radeon GPU Analyzer, so it is AMD-only and static.
This is the other half: it asks whichever driver actually compiled the pipeline, via
VK_KHR_pipeline_executable_properties, so an NVIDIA adapter reports too and a cross-vendor register
comparison becomes possible. It needs the device, so it is a local tool, not a CI gate, and it does not
replace RGA.

    py Scripts/shader-stats.py                      # capture on the default adapter, report
    py Scripts/shader-stats.py --gpu 5070           # pin the adapter
    py Scripts/shader-stats.py --compare 9070 5070  # capture both, print the cross-vendor table
    py Scripts/shader-stats.py --json <path>        # report an existing capture, no GPU run
    py Scripts/shader-stats.py --compare 9070 5070 --frames 400 --update-baseline

COVERAGE IS WHAT THE RUN BUILT, not the shader set, and this is the sharpest difference from RGA.
Pipelines are created lazily, so a capture contains only what the app actually compiled: measured, two
identical back-to-back runs on the 9070 XT differed (21 vs 22 executables, the second additionally
creating DefaultLit's [noinlineshadow] permutation). RGA cooks every shader and every permutation
offline and is deterministic, so RGA stays the authority for AMD coverage and for anything gated. Use
this for NVIDIA, where no offline analyser exists, and for the cross-vendor comparison on shaders that
appear in both captures. --frames 400 is what reliably builds the RT and denoise pipelines; the 90
default leaves GI.comp and Reflection.comp out.

Baselines under Scripts/shader-stats-baseline/<device-slug>.json are the citable record: without them
these numbers live only in a gitignored Engine/cache file, which no reader can check.

PERMUTATIONS ARE NOT LABELLED HERE, and cannot be. The driver reports an executable against the pipeline
it belongs to, and every permutation of a shader shares one source path, so DefaultLit's [inlineshadow]
and [noinlineshadow] variants both appear as "DefaultLit.frag.hlsl" with only their register counts (127
and 87 on gfx1200) telling them apart. GIDenoise.comp likewise has three permutations ([ao], [gi],
[shadow]) that are indistinguishable by name. RGA cooks each permutation separately and labels it, so
carrying RGA's labels onto these rows is only justified because the AMD register counts agree between the
two tools; do not carry them onto NVIDIA rows, where there is nothing to cross-check against.

WHAT EACH VENDOR REPORTS DIFFERS, and the tool does not pretend otherwise. AMD gives numUsedVgprs /
numUsedSgprs / ldsUsageSizeInBytes / scratchMemUsageInBytes; NVIDIA gives Register Count / Binary Size /
Local Memory Size / Stack Size. Only the register count and the spill indicator have a common meaning,
so only those are placed side by side; everything else is printed per-vendor.

CONSTANT COLUMNS ARE FLAGGED, NOT REPORTED. A statistic with one value across every executable carries
no information, and at least one is actively wrong: the RTX 5070 driver declares "Local Memory Size" as
uint64 and returns 68719476736 (2^36, low 32 bits zero) for every shader, which is a union-discriminator
artifact rather than a 64 GiB spill. Reading it as a spill figure would be a fabricated number, so the
value is suppressed and the column named.
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

# (common name, AMD key, NVIDIA key). Only stats whose MEANING matches across vendors belong here.
COMMON = [
    ("registers", "numUsedVgprs", "Register Count"),
    ("spill bytes", "scratchMemUsageInBytes", "Local Memory Size"),
]

# The rung that actually builds the interesting pipelines. A bare editor run creates only what it draws
# (12 executables: depth, sky, tonemap, lit, shadow), so the denoiser and RT compute pipelines never
# exist and never report. This turns them on.
# Mode values are numeric and must match perf-bench's CONFIGS; this is its "+gi" rung (2 = ray traced).
CAPTURE_ENV = {
    "SS_RENDER_SHADOWS_MODE": "2",
    "SS_RENDER_AO_MODE": "2",
    "SS_RENDER_REFLECTIONS_MODE": "2",
    "SS_RENDER_GI_MODE": "2",
}


def device_slug(device: str) -> str:
    """Filesystem-safe directory name for an adapter, mirroring perf-bench's scheme."""
    slug = re.sub(r"[^a-z0-9]+", "-", device.lower()).strip("-")
    return slug or "unknown-device"


def baseline_path(repo_root: Path, device: str) -> Path:
    return repo_root / "Scripts" / "shader-stats-baseline" / f"{device_slug(device)}.json"


def write_baseline(repo_root: Path, doc: dict) -> Path:
    """Commit the capture so the numbers are a checkable artifact, not just prose.

    Every other gate here has a committed baseline directory; without one these numbers exist only in
    a gitignored Engine/cache file and whatever was written about them, which is not reproducible by
    a reader. This is NOT a gate (see the module docstring): it is the citable record.
    """
    out = baseline_path(repo_root, doc["device"])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="")
    return out


def capture(repo_root: Path, exe: Path, gpu: str, frames: int, timeout: int) -> dict | None:
    out = repo_root / "Engine" / "cache" / f"shader-stats-{gpu or 'auto'}.json"
    out.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(CAPTURE_ENV)
    env.update({
        "SS_SHADER_STATS": "1",
        "SS_SHADER_STATS_PATH": str(out),
        "SS_SMOKE_FRAMES": str(frames),
        "SS_CONFIG_IGNORE": "1",
        "SS_VALIDATION_NONFATAL": "1",
    })
    if gpu:
        env["SS_RENDER_GPU"] = gpu
    layers = repo_root / "vcpkg" / "installed" / "x64-windows" / "bin"
    if layers.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layers)

    proc = subprocess.run([str(exe)], cwd=str(repo_root), env=env, capture_output=True,
                          text=True, timeout=timeout)
    if proc.returncode != 0:
        print(f"  capture failed (exit {proc.returncode})")
        return None
    if "not supported by this driver" in proc.stdout:
        print("  VK_KHR_pipeline_executable_properties unsupported on this adapter.")
        return None
    if not out.is_file():
        print("  no statistics written (extension missing, or no pipelines built).")
        return None
    return json.loads(out.read_text(encoding="utf-8"))


def classify(doc: dict) -> tuple[set, set, dict]:
    """Split stats into (all-zero, suspect, notes).

    Three cases, and conflating them is how a fabricated number gets published.

    All-zero across every executable is a real, useful result ("nothing spilled anywhere"), reported
    as a note rather than a column.

    A constant NON-zero value carries no per-shader information and is the signature of a driver
    returning something other than data.

    Constant non-zero HIGH 32 bits with varying low 32 bits is the specific, recognisable case of a
    32-bit value written into the 64-bit union without clearing the upper half. The RTX 5070 does this
    for "Local Memory Size": every value is 0x10_00000000 + {0,16,32,48}. The low half is almost
    certainly the real spill size, but "almost certainly" is not a measurement, so the column is
    withheld with the pattern named rather than silently masked.
    """
    vals: dict[str, list] = {}
    for e in doc["executables"]:
        for k, v in e["stats"].items():
            vals.setdefault(k, []).append(v.get("value"))

    zero: set = set()
    suspect: set = set()
    notes: dict[str, str] = {}
    n = len(doc["executables"])
    for k, vs in vals.items():
        ints = [v for v in vs if isinstance(v, int) and not isinstance(v, bool)]
        if len(ints) == len(vs) and ints:
            highs = {v >> 32 for v in ints}
            if len(highs) == 1 and next(iter(highs)) != 0:
                high = next(iter(highs))
                low = sorted({v & 0xFFFFFFFF for v in ints})
                suspect.add(k)
                notes[k] = (f"constant non-zero high 32 bits (0x{high:x}) with low bits varying over "
                            f"{low}: a 32-bit value in a 64-bit union with the upper half uncleared. "
                            f"Withheld rather than masked, since the mask is inference.")
                continue
        if n < 2:
            continue
        uniq = {json.dumps(v) for v in vs}
        if len(uniq) == 1:
            (only,) = uniq
            if only in ("0", "0.0", "false", "null"):
                zero.add(k)
            else:
                suspect.add(k)
                notes[k] = f"one value ({only}) across every executable, so no information."
    return zero, suspect, notes


# Theoretical, register-limited occupancy. Reported as an ESTIMATE, never gated.
#
# AMD: NOT reimplemented here. The model is imported from rga-occupancy.py, which is the GATED one, so
# there is exactly one definition of AMD occupancy in the repo. This file previously carried its own
# copy with an 8-VGPR allocation granularity, which disagreed with the gated model (12 on gfx11, 24 on
# gfx12) and with AGENTS.md, and produced 12/16 for a shader the gate scores 10/16. Two tools disagreeing
# about the same hardware is worse than either being wrong, so the duplicate is gone.
#
# Granularity matters most exactly where it is least visible: DefaultLit.frag[inlineshadow] at 127 VGPRs
# sits one count above the 6*24 = 144 allocation boundary on gfx12, so the wave count is model-dependent
# there. rga-occupancy.py's own comment flags gfx12's 24 as documentation-derived and unconfirmed per
# compile, unlike gfx11's 12 which RGA reports directly. Treat a count near a boundary accordingly.
#
# NVIDIA: compute capability 12.0 (RTX 50 consumer) per the CUDA Programming Guide's per-compute-
# capability table: 48 warps/SM and a 64K 32-bit register file per SM. 65536 registers over 48*32 = 1536
# threads is ~42 registers per thread for full occupancy, so register pressure bites EARLIER on NVIDIA
# than the raw counts suggest. This IS an upper bound: the allocation granularity for 12.0 is not in the
# published table, and rounding up can only lower the warp count. It also ignores shared memory, warp
# allocation granularity and pixel-shader attribute storage. Measure achieved occupancy with Nsight
# Graphics GPU Trace.
NV_REGS_PER_SM, NV_MAX_WARPS, NV_WARP_SIZE = 65536, 48, 32

# Device name substring -> RGA ASIC target, for adapters this repo actually evaluates. An unrecognised
# AMD adapter yields NO occupancy figure rather than a wrong one, matching rga-occupancy.py's stance on
# unmodelled architectures. Override with --asic.
AMD_ASIC_BY_DEVICE = {"9070": "gfx1200", "9060": "gfx1200", "7900": "gfx1100"}

_rga_module = None


def rga_model():
    """Lazily import rga-occupancy.py for vgpr_occupancy(). Its bootstrap lives inside functions, so
    importing it runs no RGA download."""
    global _rga_module
    if _rga_module is None:
        spec = importlib.util.spec_from_file_location(
            "rga_occupancy", Path(__file__).resolve().parent / "rga-occupancy.py")
        _rga_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_rga_module)
    return _rga_module


def amd_asic(device: str, override: str = "") -> str | None:
    if override:
        return override
    d = device.lower()
    return next((a for sub, a in AMD_ASIC_BY_DEVICE.items() if sub in d), None)


def occupancy(regs, is_amd: bool, asic: str | None = None) -> tuple[int, int] | None:
    """(waves_or_warps, max), or None when no model applies. AMD delegates to the gated model."""
    if regs is None or regs <= 0:
        return None
    if is_amd:
        if asic is None:
            return None
        waves = rga_model().vgpr_occupancy(float(regs), asic)
        return None if waves is None else (waves, 16)
    return min(NV_MAX_WARPS, NV_REGS_PER_SM // (int(regs) * NV_WARP_SIZE)), NV_MAX_WARPS


def is_amd_device(doc: dict) -> bool:
    d = doc["device"].lower()
    return "amd" in d or "radeon" in d


def val(e: dict, key: str):
    s = e["stats"].get(key)
    return None if s is None else s.get("value")


def report(doc: dict) -> None:
    zero, suspect, notes = classify(doc)
    const = zero | suspect
    print(f"=== {doc['device']} ===  ({len(doc['executables'])} executables, via {doc['source']})")
    names = sorted({k for e in doc["executables"] for k in e["stats"]})
    live = [n for n in names if n not in const]
    print(f"  {'shader':<34}{'stage':<14}" + "".join(f"{n[:15]:>17}" for n in live))
    for e in sorted(doc["executables"], key=lambda x: (x["pipeline"], x["stages"])):
        row = "".join(f"{str(val(e, n)):>17}" for n in live)
        print(f"  {Path(e['pipeline']).name[:33]:<34}{e['stages'][:13]:<14}{row}")
    if zero:
        print(f"  ZERO on every executable (a real result, not an artifact): {', '.join(sorted(zero))}")
    for k in sorted(suspect):
        print(f"  WITHHELD  {k}: {notes[k]}")
    print()


def compare(a: dict, b: dict, asic_a: str = "", asic_b: str = "") -> None:
    def keyof(doc, common_idx):
        amd = "amd" in doc["device"].lower() or "radeon" in doc["device"].lower()
        return COMMON[common_idx][1] if amd else COMMON[common_idx][2]

    za, sa, na = classify(a)
    zb, sb, nb = classify(b)
    idx = {}
    for doc, slot in ((a, 0), (b, 1)):
        for e in doc["executables"]:
            idx.setdefault((Path(e["pipeline"]).name, e["stages"]), [None, None])[slot] = e

    print(f"=== cross-vendor: {a['device']}  vs  {b['device']} ===")
    for cname, _, _ in COMMON:
        i = [c[0] for c in COMMON].index(cname)
        ka, kb = keyof(a, i), keyof(b, i)
        if ka in sa or kb in sb:
            who, why = (f"{a['device']}:{ka}", na.get(ka, "")) if ka in sa else (f"{b['device']}:{kb}", nb.get(kb, ""))
            print(f"  {cname}: WITHHELD on {who} -- {why}")
            continue
        if ka in za and kb in zb:
            print(f"  {cname}: zero on both adapters for every shader (nothing to compare, and that is the result)")
            continue
        print(f"  --- {cname} ({ka} vs {kb}) ---")
        hdr = f"    {'shader':<34}{'stage':<14}{'A':>8}{'B':>8}{'B-A':>8}"
        if cname == "registers":
            hdr += "   occupancy A   occupancy B (NVIDIA = upper bound)"
        print(hdr)
        for (name, stage), (ea, eb) in sorted(idx.items()):
            if ea is None or eb is None:
                continue
            va, vb = val(ea, ka), val(eb, kb)
            if va is None or vb is None:
                continue
            extra = ""
            if cname == "registers":
                oa = occupancy(va, is_amd_device(a), amd_asic(a["device"], asic_a))
                ob = occupancy(vb, is_amd_device(b), amd_asic(b["device"], asic_b))
                fmt = lambda o: "      n/a" if o is None else f"{o[0]:>2}/{o[1]:<2} {o[0] / o[1] * 100:>3.0f}%"
                extra = f"   {fmt(oa)}   {fmt(ob)}"
            print(f"    {name[:33]:<34}{stage[:13]:<14}{va:>8}{vb:>8}{vb - va:>+8}{extra}")
    print()


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gpu", default="", help="Adapter name substring or index (render.gpu)")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"), help="Capture both adapters and diff them")
    ap.add_argument("--json", help="Report an existing capture instead of running")
    ap.add_argument("--frames", type=int, default=90, help="Frames to run so pipelines get built (default 90)")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--asic", default="",
                    help="Override the AMD ASIC target for occupancy (e.g. gfx1200); default maps from device name")
    ap.add_argument("--update-baseline", action="store_true",
                    help="Write each capture to Scripts/shader-stats-baseline/<device-slug>.json")
    args = ap.parse_args()

    if args.json:
        report(json.loads(Path(args.json).read_text(encoding="utf-8")))
        return 0

    exe = repo_root / "build" / "Snowstorm-Editor" / args.config / "Snowstorm-Editor.exe"
    if not exe.is_file():
        print(f"FAIL: {exe} not found. Build first.")
        return 1

    if args.compare:
        docs = []
        for g in args.compare:
            print(f"capturing on '{g}' ...")
            d = capture(repo_root, exe, g, args.frames, args.timeout)
            if d is None:
                return 2
            if args.update_baseline:
                print(f"  wrote {write_baseline(repo_root, d).relative_to(repo_root)}")
            docs.append(d)
        for d in docs:
            report(d)
        compare(docs[0], docs[1], args.asic, args.asic)
        return 0

    doc = capture(repo_root, exe, args.gpu, args.frames, args.timeout)
    if doc is None:
        return 2
    if args.update_baseline:
        print(f"  wrote {write_baseline(repo_root, doc).relative_to(repo_root)}")
    report(doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
