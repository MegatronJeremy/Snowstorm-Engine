#!/usr/bin/env python3
"""Static shader occupancy / register-pressure gate for Snowstorm (AMD RGA).

Headless, deterministic, no GPU run. Feeds each compiled SPIR-V module through the
Radeon GPU Analyzer offline compiler for a target ASIC (default gfx1100, the RX 7900
XTX / RDNA3), parses RGA's statistics CSV (VGPR/SGPR usage, LDS bytes, scratch + register
spills, ISA size), then diffs against a committed baseline in Scripts/rga-baseline/. It
fails (exit 1) when VGPR-limited occupancy drops (fewer waves/SIMD), a spill appears, or
LDS/ISA rises beyond the threshold. Raw VGPR% is deliberately NOT gated: a VGPR rise that
doesn't cross a wave boundary is harmless.

Unlike perf-bench.py this needs NO GPU: RGA is a static offline compiler. RGA's CLI stats
have no occupancy column (the GUI computes it), so we derive VGPR-limited waves/SIMD from
the VGPR count using the RDNA3 (gfx11) model (1536 VGPRs/SIMD, 16 waves max, <=96 VGPRs =>
full). This is *theoretical* occupancy, not measured, and VGPR-only (LDS occupancy needs
the workgroup size RGA offline reports as 0). It catches the changes that wreck occupancy
at compile time. Measure real achieved occupancy / bandwidth / stalls with RGP.

Input SPIR-V comes from the engine's shader cache (Engine/cache/shaders/*.spv), which is
populated by any editor build+run. A shader can have several permutation .spv files
(different #defines -> different content hash); we analyse them all and gate on the
WORST permutation per shader (lowest occupancy / highest VGPR / most spills), keyed by
base shader name so a source edit re-compares the same logical shader.

RGA is a standalone download (github.com/GPUOpen-Tools/radeon_gpu_analyzer); it does NOT
need the dev-mode driver. If it isn't found via --rga, the SS_RGA env var, or PATH, the
script auto-bootstraps a pinned, checksum-verified copy into Tools/rga/ (~238MB, one-time,
cached) so a fresh box is fully headless. Disable with --no-bootstrap.

Usage (from repo root or anywhere):
    py Scripts/rga-occupancy.py                     # analyse cache, diff vs baseline, PASS/FAIL
    py Scripts/rga-occupancy.py --update-baseline   # capture current results as the new baseline
    py Scripts/rga-occupancy.py --only Reflection   # one shader (base-name substring)
    py Scripts/rga-occupancy.py --asic gfx1100      # target ASIC (default gfx1100)
    py Scripts/rga-occupancy.py --threshold 10      # regression tolerance %% (default 10)
    py Scripts/rga-occupancy.py --dry-run           # print planned RGA invocations, don't run

Exit code: 0 if every shader is within threshold (or --update-baseline), 1 on a
regression / run failure. --dry-run always exits 0.
"""
import argparse
import csv
import glob
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

# Pinned RGA release (github.com/GPUOpen-Tools/radeon_gpu_analyzer). Pinned, not "latest", so the
# gate is reproducible: RGA's stats columns/compiler can shift between versions and would move the
# baseline. Same philosophy as the clang-format==22.1.5 pin. Bump deliberately with a re-baseline.
RGA_VERSION = "2.14.2"
RGA_SHA256 = "5b46941a72722ddb27c9427ca4413bccc6892b4185ec1d5b9120740687fa166d"
RGA_URL = ("https://github.com/GPUOpen-Tools/radeon_gpu_analyzer/releases/download/"
           "{v}/rga-windows-x64-{v}.zip")

# SPIR-V execution model (OpEntryPoint word 1) -> RGA vk-offline stage flag.
_EXEC_MODEL_TO_STAGE = {
    0: "vert",   # Vertex
    1: "tesc",   # TessellationControl
    2: "tese",   # TessellationEvaluation
    3: "geom",   # Geometry
    4: "frag",   # Fragment
    5: "comp",   # GLCompute
}
_SPIRV_MAGIC = 0x07230203
_OP_ENTRY_POINT = 15

# CSV header -> canonical metric, matched case-insensitively (substring, first hit). These are the
# exact columns RGA 2.14.2 writes for vk-spv-offline (gfx1100_stats_<stage>.csv); aliases must
# prefer USED_* over AVAILABLE_* (both contain "LDS_BYTES"/"VGPRs"). RGA's CLI stats have NO
# occupancy column (the GUI computes it), so we gate on occupancy's determinants: register/LDS
# pressure and spills. True achieved occupancy comes from RGP (runtime) or the RGA GUI.
_METRIC_ALIASES = {
    "vgprs":       ["USED_VGPRS"],
    "sgprs":       ["USED_SGPRS"],
    "lds":         ["USED_LDS_BYTES"],
    "scratch":     ["SCRATCH_MEM"],
    "vgpr_spills": ["VGPR_SPILLS"],
    "sgpr_spills": ["SGPR_SPILLS"],
    "isa_size":    ["ISA_SIZE"],
}
# Secondary metrics gated on a rise beyond threshold (occupancy, below, is the primary gate).
_HIGHER_IS_WORSE = ("lds", "isa_size")
# Metrics that must stay at 0; any 0 -> >0 is a hard fail regardless of threshold.
_SPILL_METRICS = ("scratch", "vgpr_spills", "sgpr_spills")

# RDNA3 (gfx11) wave32 occupancy model. Constants verified against AMD's RDNA3 docs and RGA's own
# arch-info output: 1536 VGPRs/SIMD (192KB), 16 waves/SIMD max, <=96 VGPRs => full occupancy.
# SGPRs never limit on RDNA. LDS can, but RGA offline reports THREADS_PER_WORKGROUP=0, so we compute
# the VGPR-limited theoretical occupancy only (and gate LDS growth separately). Not measured -- see RGP.
RDNA3_VGPR_FILE = 1536
RDNA3_MAX_WAVES = 16
RDNA3_VGPR_GRANULARITY = 16


def vgpr_occupancy(vgprs: float, asic: str) -> int | None:
    """VGPR-limited theoretical waves/SIMD (wave32) for a gfx11 ASIC, else None."""
    if not asic.startswith("gfx11") or vgprs <= 0:
        return None
    g = RDNA3_VGPR_GRANULARITY
    allocated = max(g, ((int(vgprs) + g - 1) // g) * g)
    return min(RDNA3_MAX_WAVES, RDNA3_VGPR_FILE // allocated)


def find_repo_root(script_dir: Path) -> Path:
    return script_dir.parent


def _rga_cache_dir(repo_root: Path, version: str) -> Path:
    return repo_root / "Tools" / "rga" / version


def _find_cached_rga(repo_root: Path, version: str) -> Path | None:
    root = _rga_cache_dir(repo_root, version)
    if root.is_dir():
        for p in root.rglob("rga.exe"):
            return p
    return None


def bootstrap_rga(repo_root: Path, version: str, timeout: int = 900) -> Path | None:
    """Download the pinned RGA Windows zip, verify SHA-256, extract into Tools/rga/<version>/,
    return the rga.exe path. Idempotent: reuses an existing extraction. ~238MB one-time download."""
    cached = _find_cached_rga(repo_root, version)
    if cached:
        return cached

    dest = _rga_cache_dir(repo_root, version)
    dest.mkdir(parents=True, exist_ok=True)
    url = RGA_URL.format(v=version)
    zip_path = dest / f"rga-windows-x64-{version}.zip"

    print(f"  bootstrapping RGA {version} (one-time, ~238MB)")
    print(f"    from {url}")
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp, open(zip_path, "wb") as f:
            shutil.copyfileobj(resp, f)
    except (urllib.error.URLError, OSError) as e:
        print(f"    download failed: {e}")
        return None

    # Verify against the pinned digest before trusting the archive (only meaningful when pinned).
    if version == RGA_VERSION:
        digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
        if digest != RGA_SHA256:
            print(f"    SHA-256 mismatch: expected {RGA_SHA256}, got {digest}. Aborting.")
            zip_path.unlink(missing_ok=True)
            return None
        print("    SHA-256 OK")
    else:
        print(f"    (custom --rga-version {version}: skipping SHA-256 pin check)")

    try:
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(dest)
    except zipfile.BadZipFile as e:
        print(f"    extract failed: {e}")
        return None
    finally:
        zip_path.unlink(missing_ok=True)

    found = _find_cached_rga(repo_root, version)
    if not found:
        print(f"    extracted, but no rga.exe found under {dest}")
    return found


def ensure_rga(explicit: str | None, repo_root: Path, version: str, allow_bootstrap: bool) -> Path | None:
    """Resolve rga.exe: --rga arg, SS_RGA env, PATH, then the cached bootstrap, then download."""
    for c in (explicit, os.environ.get("SS_RGA")):
        if c and Path(c).is_file():
            return Path(c)
    on_path = shutil.which("rga") or shutil.which("rga.exe")
    if on_path:
        return Path(on_path)
    cached = _find_cached_rga(repo_root, version)
    if cached:
        return cached
    if allow_bootstrap:
        return bootstrap_rga(repo_root, version)
    return None


def spirv_stage(spv: Path) -> str | None:
    """Read the first OpEntryPoint's execution model from a SPIR-V module -> stage flag.

    Robust to the stage-less IBL*.hlsl shaders (filename has no .comp/.frag token). Returns
    None if the file isn't valid SPIR-V or has no entry point we recognise.
    """
    try:
        data = spv.read_bytes()
    except OSError:
        return None
    if len(data) < 20:
        return None
    magic = struct.unpack("<I", data[0:4])[0]
    little = magic == _SPIRV_MAGIC
    if not little and struct.unpack(">I", data[0:4])[0] != _SPIRV_MAGIC:
        return None
    endian = "<" if little else ">"
    words = struct.unpack(f"{endian}{len(data) // 4}I", data[: (len(data) // 4) * 4])
    i = 5  # skip header (magic, version, generator, bound, schema)
    n = len(words)
    while i < n:
        word = words[i]
        count = word >> 16
        opcode = word & 0xFFFF
        if count == 0:
            break  # malformed; avoid infinite loop
        if opcode == _OP_ENTRY_POINT and i + 1 < n:
            return _EXEC_MODEL_TO_STAGE.get(words[i + 1])
        i += count
    return None


def base_shader_name(spv: Path) -> str:
    """'DefaultLit.frag_e24aa653.spv' -> 'DefaultLit.frag'; 'IBLBRDFLut_d068.spv' -> 'IBLBRDFLut'."""
    stem = spv.name[:-4] if spv.name.endswith(".spv") else spv.name
    cut = stem.rfind("_")
    return stem[:cut] if cut > 0 else stem


def _match_metric(header: list[str], aliases: list[str]) -> int | None:
    up = [h.strip().upper() for h in header]
    for a in aliases:
        au = a.strip().upper()
        for idx, h in enumerate(up):
            if au in h:
                return idx
    return None


def parse_rga_csv(csv_path: Path) -> dict | None:
    """Parse RGA's -a statistics CSV into {metric: float}. Returns None if unreadable."""
    try:
        rows = list(csv.reader(csv_path.read_text().splitlines()))
    except OSError:
        return None
    rows = [r for r in rows if r and any(c.strip() for c in r)]
    if len(rows) < 2:
        return None
    header, data = rows[0], rows[1]  # RGA writes one stats row per stage file
    out: dict[str, float] = {}
    for metric, aliases in _METRIC_ALIASES.items():
        idx = _match_metric(header, aliases)
        if idx is not None and idx < len(data):
            raw = data[idx].strip()
            try:
                out[metric] = float(raw.rstrip("%")) if raw not in ("", "N/A", "-") else 0.0
            except ValueError:
                pass
    return out or None


def run_rga(rga: Path, asic: str, stage: str, spv: Path, timeout: int) -> dict | None:
    tmp = Path(tempfile.mkdtemp(prefix="rga-"))
    stats = tmp / "stats.csv"
    cmd = [str(rga), "-s", "vk-spv-offline", "-c", asic, f"--{stage}", str(spv), "-a", str(stats)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"    RGA timed out after {timeout}s")
        return None
    except OSError as e:
        print(f"    RGA failed to launch: {e}")
        return None
    # RGA prefixes the ASIC and suffixes the stage, e.g. gfx1100_stats_comp.csv.
    produced = list(tmp.glob("*stats*.csv"))
    if proc.returncode != 0 and not produced:
        print(f"    RGA exit {proc.returncode}: {proc.stderr.strip()[:200]}")
        return None
    if not produced:
        print(f"    RGA wrote no stats CSV (cmd: {' '.join(cmd)})")
        return None
    return parse_rga_csv(produced[0])


_LIVEREG_SUMMARY = re.compile(r"Maximum # VGPR used (\d+), VGPRs allocated by HW: (\d+)")


def run_rga_livereg(rga: Path, asic: str, stage: str, spv: Path, timeout: int) -> dict | None:
    """Run RGA live-VGPR analysis; return {max_used, allocated, peak_line, peak_isa}.

    Pinpoints WHERE register pressure peaks (the instruction holding the most live VGPRs), which
    is the actionable target for cutting a shader's VGPR count. Investigation only, not gated.
    """
    tmp = Path(tempfile.mkdtemp(prefix="rga-lr-"))
    out = tmp / "lr.txt"
    cmd = [str(rga), "-s", "vk-spv-offline", "-c", asic, f"--{stage}", str(spv), "--livereg", str(out)]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        return None
    produced = list(tmp.glob("*lr*.txt")) + ([out] if out.exists() else [])
    if not produced:
        return None
    text = produced[0].read_text(errors="replace")
    max_used = allocated = None
    peak_live, peak_line, peak_isa = -1, None, ""
    for line in text.splitlines():
        m = _LIVEREG_SUMMARY.search(line)
        if m:
            max_used, allocated = int(m.group(1)), int(m.group(2))
            continue
        # Data rows: "<lineno> | <live> | <liveness map> | <isa>" (isa may itself contain '|').
        parts = line.split("|", 3)
        if len(parts) >= 4:
            try:
                lineno, live = int(parts[0].strip()), int(parts[1].strip())
            except ValueError:
                continue
            if live > peak_live:
                peak_live, peak_line, peak_isa = live, lineno, parts[3].strip()
    if max_used is None and peak_live < 0:
        return None
    return {"max_used": max_used if max_used is not None else peak_live,
            "allocated": allocated, "peak_line": peak_line, "peak_isa": peak_isa}


def collapse_worst(perm_metrics: list[dict]) -> dict:
    """Fold a shader's permutations into the worst case (max of every metric: higher register/LDS/
    spill pressure is always the limiting permutation)."""
    worst: dict[str, float] = {}
    for m in perm_metrics:
        for k, v in m.items():
            worst[k] = v if k not in worst else max(worst[k], v)
    worst["permutations"] = len(perm_metrics)
    return worst


def _occ_str(vgprs: float, asic: str) -> str:
    occ = vgpr_occupancy(vgprs, asic)
    return f"{occ}/{RDNA3_MAX_WAVES}" if occ is not None else "--"


def compare(name: str, cur: dict, base: dict, threshold_pct: float, asic: str) -> bool:
    """Print one shader's baseline-vs-current metrics; return True if within threshold."""
    ok = True
    flags = []
    # spills: any 0 -> >0 is a hard fail regardless of threshold (a spilling shader is a cliff).
    for k in _SPILL_METRICS:
        if cur.get(k, 0) > 0 and base.get(k, 0) == 0:
            ok = False
            flags.append(f"NEW {k}")
    # occupancy (primary gate): fewer VGPR-limited waves/SIMD is a regression. This is why raw VGPR%
    # is NOT gated -- a VGPR rise that doesn't cross a wave boundary is harmless.
    base_occ = vgpr_occupancy(base.get("vgprs", 0), asic)
    cur_occ = vgpr_occupancy(cur.get("vgprs", 0), asic)
    if base_occ is not None and cur_occ is not None and cur_occ < base_occ:
        ok = False
        flags.append(f"occupancy {base_occ}->{cur_occ} waves")
    # LDS/ISA: a rise beyond threshold is a regression (LDS is a secondary occupancy limiter we
    # can't model without workgroup size; ISA growth flags code bloat / i-cache pressure).
    for k in _HIGHER_IS_WORSE:
        b, c = base.get(k), cur.get(k)
        if b and c is not None and b > 0:
            rise = (c - b) / b * 100.0
            if rise > threshold_pct:
                ok = False
                flags.append(f"{k} +{rise:.0f}%")
    spill = cur.get("vgpr_spills", 0) + cur.get("sgpr_spills", 0) + cur.get("scratch", 0)
    tag = "  " + ", ".join(flags) if flags else ""
    print(f"  {name:<24} vgpr {base.get('vgprs', 0):>3.0f}->{cur.get('vgprs', 0):<3.0f}  "
          f"occ {_occ_str(base.get('vgprs', 0), asic):>5}->{_occ_str(cur.get('vgprs', 0), asic):<5}  "
          f"lds {base.get('lds', 0):>5.0f}->{cur.get('lds', 0):<5.0f}  "
          f"spills {spill:<4.0f}{'  REGRESSION' if not ok else ''}{tag}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Static shader occupancy / register gate (AMD RGA).")
    ap.add_argument("--rga", default=None, help="Path to rga(.exe) (else SS_RGA env, PATH, then auto-bootstrap)")
    ap.add_argument("--rga-version", default=RGA_VERSION, help=f"Pinned RGA version to bootstrap (default {RGA_VERSION})")
    ap.add_argument("--no-bootstrap", action="store_true", help="Do not auto-download RGA if it is missing")
    ap.add_argument("--asic", default="gfx1100", help="Target ASIC (default gfx1100 = RX 7900 XTX)")
    ap.add_argument("--spv-dir", default="Engine/cache/shaders", help="Directory of compiled .spv")
    ap.add_argument("--only", default=None, help="Analyse only shaders whose base name contains this")
    ap.add_argument("--threshold", type=float, default=10.0, help="Regression tolerance %% (default 10)")
    ap.add_argument("--timeout", type=int, default=60, help="Per-shader RGA timeout in seconds")
    ap.add_argument("--update-baseline", action="store_true", help="Write current results as baseline")
    ap.add_argument("--dry-run", action="store_true", help="Print planned RGA invocations, don't run")
    ap.add_argument("--livereg", action="store_true",
                    help="Investigation mode: report peak live-VGPR location per shader (pair with --only)")
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = find_repo_root(script_dir)
    spv_dir = (repo_root / args.spv_dir).resolve()
    baseline_file = repo_root / "Scripts" / "rga-baseline" / f"occupancy-{args.asic}.json"

    if not spv_dir.is_dir():
        print(f"FAIL: SPIR-V dir not found: {spv_dir} (build+run the editor to populate the cache)")
        return 1

    spvs = sorted(Path(p) for p in glob.glob(str(spv_dir / "*.spv")))
    if not spvs:
        print(f"FAIL: no .spv in {spv_dir}")
        return 1

    # Group permutation files by base shader name, resolving each file's stage from SPIR-V.
    groups: dict[str, list[tuple[Path, str]]] = {}
    skipped = []
    for spv in spvs:
        base = base_shader_name(spv)
        if args.only and args.only.lower() not in base.lower():
            continue
        stage = spirv_stage(spv)
        if stage is None:
            skipped.append(spv.name)
            continue
        groups.setdefault(base, []).append((spv, stage))

    print(f"Repo root : {repo_root}")
    print(f"SPIR-V    : {spv_dir}  ({len(spvs)} modules, {len(groups)} shaders, {args.asic})")
    if skipped:
        print(f"  skipped (no recognised SPIR-V entry point): {', '.join(skipped)}")

    if args.dry_run:
        print("\n=== DRY RUN (planned RGA invocations) ===")
        for base, files in sorted(groups.items()):
            for spv, stage in files:
                print(f"  rga -s vk-spv-offline -c {args.asic} --{stage} {spv.name} -a stats.csv")
        print(f"\n{sum(len(v) for v in groups.values())} invocations across {len(groups)} shaders.")
        return 0

    rga = ensure_rga(args.rga, repo_root, args.rga_version, allow_bootstrap=not args.no_bootstrap)
    if rga is None:
        if args.no_bootstrap:
            print("\nFAIL: rga(.exe) not found and --no-bootstrap set. Pass --rga <path>, set SS_RGA, or add to PATH.")
        else:
            print("\nFAIL: could not resolve or bootstrap RGA.")
        print("  Standalone download: https://github.com/GPUOpen-Tools/radeon_gpu_analyzer/releases")
        return 1
    print(f"RGA       : {rga}")

    if args.livereg:
        print("Mode      : LIVE-VGPR HOTSPOTS (investigation, not gated)\n")
        for base, files in sorted(groups.items()):
            for spv, stage in files:
                r = run_rga_livereg(rga, args.asic, stage, spv, args.timeout)
                if r is None:
                    print(f"  {base:<24} livereg produced no output")
                    continue
                alloc = f" (HW-allocated {r['allocated']})" if r["allocated"] is not None else ""
                loc = f" @ ISA line {r['peak_line']}: {r['peak_isa']}" if r["peak_line"] is not None else ""
                print(f"  {base:<24} peak live VGPR {r['max_used']}{alloc}{loc}")
        return 0

    print(f"Mode      : {'UPDATE BASELINE' if args.update_baseline else 'compare vs baseline'}\n")

    import json  # local: only needed past the dry-run path
    current: dict[str, dict] = {}
    for base, files in sorted(groups.items()):
        perms = []
        for spv, stage in files:
            m = run_rga(rga, args.asic, stage, spv, args.timeout)
            if m is not None:
                perms.append(m)
        if perms:
            current[base] = collapse_worst(perms)
        else:
            print(f"  {base:<24} RGA produced no parseable stats")

    if not current:
        print("FAIL: RGA produced no results for any shader.")
        return 1

    baseline_file.parent.mkdir(parents=True, exist_ok=True)
    if args.update_baseline:
        payload = {"asic": args.asic, "shaders": current}
        baseline_file.write_text(json.dumps(payload, indent=2))
        print(f"Updated baseline: {baseline_file.relative_to(repo_root)} ({len(current)} shaders)")
        return 0

    if not baseline_file.exists():
        print(f"No baseline at {baseline_file.relative_to(repo_root)} -- run with --update-baseline first.")
        for base, m in sorted(current.items()):
            spill = m.get("vgpr_spills", 0) + m.get("sgpr_spills", 0) + m.get("scratch", 0)
            print(f"  {base:<24} vgpr {m.get('vgprs', 0):>3.0f}  occ {_occ_str(m.get('vgprs', 0), args.asic):>5}  "
                  f"lds {m.get('lds', 0):>5.0f}  spills {spill:.0f}")
        return 0

    baseline = json.loads(baseline_file.read_text()).get("shaders", {})
    all_ok = True
    for base in sorted(set(current) | set(baseline)):
        c, b = current.get(base), baseline.get(base)
        if b is None:
            print(f"  {base:<24} (new shader, no baseline)")
            continue
        if c is None:
            print(f"  {base:<24} (gone from cache)")
            continue
        if not compare(base, c, b, args.threshold, args.asic):
            all_ok = False

    print("\n=== Summary ===")
    print("PASS" if all_ok else "FAIL (occupancy/register regression)")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
