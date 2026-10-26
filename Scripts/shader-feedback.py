#!/usr/bin/env python3
"""Compiler feedback for shaders: per-shader RGA statistics as JSON, and a base-vs-branch diff.

A static proxy. It shortlists ideas (registers, LDS, scratch, theoretical occupancy, instruction
count per shader and stage on each target ASIC); it is not a merge gate and says nothing about
achieved speed. Confirm a candidate with real timings (perf-bench.py --compare-exe) before keeping it.
Needs no GPU. The compile path is the normal one: cook-shaders.py (the runtime's dxc flags) to SPIR-V,
then Radeon GPU Analyzer, reusing the pinned bootstrap, SPIR-V parsing and occupancy model of
rga-occupancy.py.

    py Scripts/cook-shaders.py --variants rt,base
    py Scripts/shader-feedback.py report --asic gfx1151,gfx1200 --spv-dir Engine/cache/shaders-cook --out head.json
    py Scripts/shader-feedback.py diff base.json head.json --markdown comment.md

`report` prints the JSON to stdout when --out is omitted. `--only <substr>` narrows to shaders whose
base name contains it. An ASIC the pinned RGA cannot compile for is recorded under "errors" and the
other ASICs still report. `diff` lists only shaders whose numbers changed, new and removed shaders;
`--markdown` writes a PR comment. Both exit 0 on a clean run; a failed RGA run in `report` exits 1.
The `instructions` count is parsed from RGA's ISA listing and is null when the listing is not found.
"""
import argparse
import importlib.util
import glob
import json
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

_spec = importlib.util.spec_from_file_location("rga_occ", Path(__file__).resolve().parent / "rga-occupancy.py")
rga_occ = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rga_occ)

SCHEMA = 1
MARKER = "<!-- shader-feedback -->"
_CACHE_TAG = "+isa1"

# Metrics shown in the diff, with whether a higher value is worse.
_DIFF_METRICS = (("vgprs", True), ("sgprs", True), ("lds", True), ("scratch", True),
                 ("vgpr_spills", True), ("sgpr_spills", True), ("waves", False), ("instructions", True))

_ISA_MNEMONIC = re.compile(
    r"^\s*((?:s|v|ds|buffer|global|flat|scratch|image|exp|lds|tbuffer|mubuf|mimg)_[a-z0-9_]+|exp)\b")


def count_isa_instructions(text: str) -> int:
    """Instruction lines in an RGA ISA listing: a mnemonic first, no label, no comment."""
    n = 0
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(("//", ";", "#")) or s.endswith(":"):
            continue
        if _ISA_MNEMONIC.match(line):
            n += 1
    return n


def run_rga_with_isa(rga: Path, asic: str, stage: str, spv: Path, timeout: int) -> tuple[dict | None, str | None]:
    """One RGA compile producing both the stats CSV and the ISA listing. Returns (metrics, error)."""
    tmp = Path(tempfile.mkdtemp(prefix="rga-fb-"))
    try:
        cmd = [str(rga), "-s", "vk-spv-offline", "-c", asic, f"--{stage}", str(spv),
               "-a", str(tmp / "stats.csv"), "--isa", str(tmp / "isa.txt")]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None, f"timeout after {timeout}s"
        except OSError as e:
            return None, f"failed to launch RGA: {e}"
        csvs = list(tmp.glob("*stats*.csv"))
        if not csvs:
            msg = (proc.stderr or proc.stdout).strip().splitlines()
            return None, f"exit {proc.returncode}: {msg[-1][:200] if msg else 'no stats CSV'}"
        metrics = rga_occ.parse_rga_csv(csvs[0])
        if metrics is None:
            return None, "unreadable stats CSV"
        isa = [p for p in tmp.iterdir() if p.is_file() and p.suffix != ".csv"]
        metrics["instructions"] = (count_isa_instructions(max(isa, key=lambda p: p.stat().st_size)
                                                          .read_text(errors="replace")) if isa else None)
        return metrics, None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def shader_entry(stage: str, perms: list[dict], asic: str) -> dict:
    """Fold a shader's permutations into its worst case and derive theoretical occupancy."""
    worst = rga_occ.collapse_worst([{k: v for k, v in m.items() if v is not None} for m in perms])
    counts = [m.get("instructions") for m in perms]
    entry = {"stage": stage, "permutations": len(perms)}
    for key in ("vgprs", "sgprs", "lds", "scratch", "vgpr_spills", "sgpr_spills", "isa_size", "instructions"):
        entry[key] = int(worst[key]) if key in worst else None
    if not all(c is not None for c in counts):
        entry["instructions"] = None
    entry["waves"] = rga_occ.vgpr_occupancy(worst.get("vgprs", 0), asic)
    entry["max_waves"] = rga_occ.max_waves_for(asic)
    return entry


def build_report(asic_results: dict, errors: dict) -> dict:
    return {"schema": SCHEMA, "asics": asic_results, "errors": errors}


def _delta(b, h):
    return None if b is None or h is None else h - b


def diff_reports(base: dict, head: dict) -> dict:
    """{asic: {"changed": {shader: {metric: (base, head)}}, "added": [...], "removed": [...]}}."""
    out = {}
    for asic in sorted(set(base.get("asics", {})) | set(head.get("asics", {}))):
        b = base.get("asics", {}).get(asic, {})
        h = head.get("asics", {}).get(asic, {})
        changed = {}
        for name in sorted(set(b) & set(h)):
            rows = {m: (b[name].get(m), h[name].get(m)) for m, _ in _DIFF_METRICS
                    if b[name].get(m) != h[name].get(m)}
            if rows:
                changed[name] = rows
        out[asic] = {"changed": changed, "added": sorted(set(h) - set(b)), "removed": sorted(set(b) - set(h))}
    return out


def _fmt(b, h, worse_if_higher):
    if b is None or h is None:
        return f"{b if b is not None else 'n/a'} → {h if h is not None else 'n/a'}"
    d = h - b
    mark = "" if d == 0 else (" ⚠" if (d > 0) == worse_if_higher else " ✓")
    return f"{b} → {h} ({d:+d}){mark}"


def render_markdown(diff: dict, head: dict, base: dict) -> str:
    lines = [MARKER, "### Shader compiler feedback (RGA, static)", "",
             "Information only, not a merge gate. Registers, LDS, scratch and theoretical occupancy "
             "shortlist ideas; `perf-bench.py --compare-exe` confirms them with real timings. "
             "Worst case across a shader's permutations; ⚠ marks a rise in cost, ✓ a fall.", ""]
    quiet = True
    for asic, d in diff.items():
        if not (d["changed"] or d["added"] or d["removed"]):
            continue
        quiet = False
        lines += [f"**{asic}**", "", "| shader | metric | base → head |", "|---|---|---|"]
        worse = dict(_DIFF_METRICS)
        for name, rows in d["changed"].items():
            for m, (b, h) in rows.items():
                lines.append(f"| `{name}` | {m} | {_fmt(b, h, worse[m])} |")
        for name in d["added"]:
            e = head["asics"][asic][name]
            lines.append(f"| `{name}` | new | vgprs {e['vgprs']}, waves {e['waves']}, lds {e['lds']}, "
                         f"scratch {e['scratch']} |")
        for name in d["removed"]:
            lines.append(f"| `{name}` | removed | |")
        lines.append("")
    if quiet:
        lines.append("No change in any shader's registers, LDS, scratch, occupancy or instruction count.")
    errs = {**base.get("errors", {}), **head.get("errors", {})}
    if errs:
        lines += ["", "Not analysed: " + "; ".join(f"{a}: {e}" for a, e in sorted(errs.items()))]
    return "\n".join(lines) + "\n"


def cmd_report(args) -> int:
    repo_root = rga_occ.find_repo_root(Path(__file__).resolve().parent)
    spv_dir = (repo_root / args.spv_dir).resolve()
    spvs = sorted(Path(p) for p in glob.glob(str(spv_dir / "*.spv")))
    if not spvs:
        print(f"FAIL: no .spv in {spv_dir}", file=sys.stderr)
        return 1
    groups: dict[str, list[tuple[Path, str]]] = {}
    for spv in spvs:
        base = rga_occ.base_shader_name(spv)
        if args.only and args.only.lower() not in base.lower():
            continue
        stage = rga_occ.spirv_stage(spv)
        if stage:
            groups.setdefault(base, []).append((spv, stage))
    rga = rga_occ.ensure_rga(args.rga, repo_root, rga_occ.RGA_VERSION, allow_bootstrap=not args.no_bootstrap)
    if rga is None:
        print("FAIL: could not resolve or bootstrap RGA", file=sys.stderr)
        return 1

    results, errors = {}, {}
    version = rga_occ.RGA_VERSION + _CACHE_TAG
    for asic in [a.strip() for a in args.asic.split(",") if a.strip()]:
        cache_file = repo_root / "Tools" / "rga" / f"feedback-cache-{asic}.json"
        cache = {} if args.no_cache else rga_occ.load_stats_cache(cache_file)
        tasks = [(b, spv, st) for b, files in sorted(groups.items()) for spv, st in files]
        keys = [rga_occ.stats_cache_key(spv, st, version) for _, spv, st in tasks]
        unique = {}
        for k, t in zip(keys, tasks):
            unique.setdefault(k, t)

        def analyse(item):
            key, (_, spv, st) = item
            if key in cache:
                return cache[key], None
            return run_rga_with_isa(rga, asic, st, spv, args.timeout)

        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            computed = dict(zip(unique, pool.map(analyse, unique.items())))
        failed = [(unique[k][1].name, e) for k, (m, e) in computed.items() if m is None]
        if failed:
            errors[asic] = f"{len(failed)} module(s) failed, first: {failed[0][0]}: {failed[0][1]}"
            continue
        fresh = {k: m for k, (m, _) in computed.items() if k not in cache}
        if fresh and not args.no_cache:
            cache.update(fresh)
            rga_occ.save_stats_cache(cache_file, cache)
        per_shader: dict[str, list[dict]] = {}
        stages = {}
        for (b, _, st), k in zip(tasks, keys):
            per_shader.setdefault(b, []).append(computed[k][0])
            stages[b] = st
        results[asic] = {b: shader_entry(stages[b], per_shader[b], asic) for b in sorted(per_shader)}

    text = json.dumps(build_report(results, errors), indent=2, sort_keys=True)
    if args.out:
        Path(args.out).write_text(text + "\n")
        print(f"wrote {args.out}: {sum(len(v) for v in results.values())} shader rows, "
              f"{len(errors)} ASIC error(s)")
    else:
        print(text)
    return 1 if errors and not results else 0


def cmd_diff(args) -> int:
    base = json.loads(Path(args.base).read_text())
    head = json.loads(Path(args.head).read_text())
    md = render_markdown(diff_reports(base, head), head, base)
    if args.markdown:
        Path(args.markdown).write_text(md)
    else:
        print(md)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("report", help="RGA statistics per shader and ASIC as JSON")
    r.add_argument("--asic", default="gfx1151,gfx1200", help="comma list (default gfx1151 = Z2 8060S, gfx1200 = 9060 XT)")
    r.add_argument("--spv-dir", default="Engine/cache/shaders-cook")
    r.add_argument("--only", default=None)
    r.add_argument("--out", default=None)
    r.add_argument("--rga", default=None)
    r.add_argument("--no-bootstrap", action="store_true")
    r.add_argument("--no-cache", action="store_true")
    r.add_argument("--timeout", type=int, default=300)
    r.add_argument("--jobs", "-j", type=int, default=4)
    r.set_defaults(fn=cmd_report)
    d = sub.add_parser("diff", help="compare two report JSONs")
    d.add_argument("base")
    d.add_argument("head")
    d.add_argument("--markdown", default=None, help="write the PR comment here instead of stdout")
    d.set_defaults(fn=cmd_diff)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
