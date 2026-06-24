#!/usr/bin/env python3
"""Smoke-test the Snowstorm executables.

Launches each app with SS_SMOKE_FRAMES set, so it boots, runs a fixed number of
frames (init -> per-frame update -> shutdown), and exits on its own. Captures the
app's stdout/stderr, enforces a wall-clock timeout (a hang -> failure rather than a
forever-stuck process), checks the process exit code, and scans the log for error
markers (spdlog '[error]'/'[critical]', Vulkan validation messages, assertion text).

This needs a real GPU + display (Vulkan), so it is a LOCAL check, not a CI job.
Run it after a build to catch runtime regressions that compilation can't.

Usage (from repo root or anywhere):
    py Scripts/smoke-test.py                 # default 120 frames, 60s timeout each
    py Scripts/smoke-test.py --frames 300    # run more frames
    py Scripts/smoke-test.py --timeout 90    # per-app wall-clock budget (seconds)
    py Scripts/smoke-test.py --config Release # test the Release build instead of Debug
    py Scripts/smoke-test.py --only Editor    # run a single target
    py Scripts/smoke-test.py --warnings-fail  # treat [warning] lines as failures too

Exit code: 0 if every target passed, 1 otherwise.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# Targets: (display name, path under build/ to the exe, working dir relative to repo root).
# Working dir is the repo root so relative Assets/... paths resolve (matches the VS
# debugger working-directory setting).
TARGETS = [
    ("Editor", "Snowstorm-Editor/{config}/Snowstorm-Editor.exe"),
    ("Runtime", "Snowstorm-Runtime/{config}/Snowstorm-Runtime.exe"),
]

# Substrings that mark a failure in captured output. Case-insensitive.
# The log pattern is "[HH:MM:SS] [level] LOGGER: msg", so error/critical lines contain
# "[error]"/"[critical]". Vulkan validation and assertion text are matched directly.
ERROR_MARKERS = [
    "[error]",
    "[critical]",
    "vulkan validation",
    "validation failed",
    "assertion failed",
    "assert failed",
    "terminate called",
    "unhandled exception",
]
WARNING_MARKERS = ["[warning]"]


def find_repo_root(script_dir: Path) -> Path:
    return script_dir.parent


def scan_output(text: str, markers: list[str]) -> list[str]:
    hits = []
    for line in text.splitlines():
        low = line.lower()
        if any(m in low for m in markers):
            hits.append(line.rstrip())
    return hits


def run_target(name: str, exe: Path, cwd: Path, frames: int, timeout: int,
               warnings_fail: bool, layer_path: Path | None) -> bool:
    print(f"\n=== {name} :: {exe.name} ===")
    if not exe.exists():
        print(f"  FAIL: executable not found at {exe}")
        print("        (build first, or check --config)")
        return False

    env = os.environ.copy()
    env["SS_SMOKE_FRAMES"] = str(frames)
    # Don't assert-and-die on the first Vulkan validation error; log them all and keep running so a
    # single smoke run surfaces every error at once. We detect failures by scanning the log below.
    env["SS_VALIDATION_NONFATAL"] = "1"
    # Point Vulkan at the vcpkg validation layers, matching Generate-Solution.py and the
    # VS debugger environment. Without this the loader can't find the layers the engine
    # requests, and init crashes instead of running -> false failures.
    if layer_path and layer_path.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layer_path)
    else:
        print(f"  note: validation layer path not found ({layer_path}); running without layers.")

    try:
        proc = subprocess.run(
            [str(exe)],
            cwd=str(cwd),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as e:
        print(f"  FAIL: timed out after {timeout}s (likely a hang/deadlock).")
        captured = (e.stdout or "") + (e.stderr or "")
        tail = "\n".join(captured.splitlines()[-15:])
        if tail:
            print("  --- last output ---")
            print(tail)
        return False

    output = (proc.stdout or "") + (proc.stderr or "")

    ok = True
    if proc.returncode != 0:
        print(f"  FAIL: exit code {proc.returncode}")
        ok = False

    error_hits = scan_output(output, ERROR_MARKERS)
    if error_hits:
        print(f"  FAIL: {len(error_hits)} error marker(s) in output:")
        for h in error_hits[:20]:
            print(f"    {h}")
        ok = False

    warn_hits = scan_output(output, WARNING_MARKERS)
    if warn_hits:
        label = "FAIL" if warnings_fail else "note"
        print(f"  {label}: {len(warn_hits)} warning(s):")
        for h in warn_hits[:20]:
            print(f"    {h}")
        if warnings_fail:
            ok = False

    if ok:
        print(f"  PASS: ran {frames} frames, exit 0, no error markers.")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Smoke-test the Snowstorm executables.")
    ap.add_argument("--frames", type=int, default=120, help="Frames to run per app (default 120)")
    ap.add_argument("--timeout", type=int, default=60, help="Per-app wall-clock timeout in seconds (default 60)")
    ap.add_argument("--config", default="Debug", help="Build config dir under build/ (default Debug)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default build)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet for the validation-layer path (default x64-windows)")
    ap.add_argument("--only", default=None, help="Run only this target by name (e.g. Editor)")
    ap.add_argument("--warnings-fail", action="store_true", help="Treat [warning] lines as failures")
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = find_repo_root(script_dir)
    build_dir = (repo_root / args.build_dir).resolve()
    layer_path = (repo_root / "vcpkg" / "installed" / args.triplet / "bin").resolve()

    targets = TARGETS
    if args.only:
        targets = [t for t in TARGETS if t[0].lower() == args.only.lower()]
        if not targets:
            print(f"No target named '{args.only}'. Known: {[t[0] for t in TARGETS]}")
            return 1

    print(f"Repo root : {repo_root}")
    print(f"Build dir : {build_dir}  (config: {args.config})")
    print(f"Frames    : {args.frames}   Timeout: {args.timeout}s/app")

    results = {}
    for name, rel in targets:
        exe = build_dir / rel.format(config=args.config)
        results[name] = run_target(name, exe, repo_root, args.frames, args.timeout,
                                    args.warnings_fail, layer_path)

    print("\n=== Summary ===")
    all_ok = True
    for name, ok in results.items():
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")
        all_ok = all_ok and ok

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
