"""Cognitive complexity gate for the engine's own C++ (AGENTS.md, Function complexity).

Scores every function with clang-tidy's readability-function-cognitive-complexity, using a compile
database from a Ninja configure of this repository (build-tidy/). A function over the limit fails
unless the committed baseline already holds it over the limit and it has not got worse. Tests and
vendored code are not scored.

Usage:
  py Scripts/complexity-check.py                     check, exit 0 pass / 1 fail / 2 skip
  py Scripts/complexity-check.py --list              also print every scored function, highest first
  py Scripts/complexity-check.py --update-baseline   record the functions over the limit now

Exit 2 when it could not measure everything: no clang-tidy, no compile database, or a parse error
inside the repository's own source. Errors inside third-party or MSVC STL headers are counted and
reported, not a skip: clang still parses the repository's function bodies around them.

Windows only, like the engine. Stdlib only.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE = REPO / "Scripts" / "complexity-baseline.json"
LIMIT = 25

SCORED = re.compile(r"^(Snowstorm-Core/Source|Snowstorm-Editor/Source|Snowstorm-Runtime|Games)/")
NOT_SCORED = re.compile(r"(^|/)(Snowstorm-Tests|vcpkg|build[^/]*)/|/imgui_impl_[^/]*$")
WARNING = re.compile(r"^(.*?):(\d+):\d+: warning: function '(.+?)' has cognitive complexity of (\d+)")
ERROR = re.compile(r"^(.*?):\d+:\d+: error: (.*)$")
TIDY_CONFIG = ("{CheckOptions: {readability-function-cognitive-complexity.Threshold: 0, "
               "readability-function-cognitive-complexity.DescribeBasicIncrements: false, "
               "readability-function-cognitive-complexity.IgnoreMacros: true}}")


def rel(path: str) -> str | None:
    p = Path(path.replace("\\", "/"))
    try:
        return p.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return None


def is_scored(path: str) -> bool:
    r = rel(path)
    return r is not None and bool(SCORED.search(r)) and not NOT_SCORED.search(r)


def evaluate(scores: dict[str, int], baseline: dict[str, int], limit: int) -> tuple[list[str], list[str]]:
    """scores and baseline are keyed 'path::function'. Returns (failures, notes)."""
    failures = []
    for key, value in sorted(scores.items(), key=lambda item: -item[1]):
        before = baseline.get(key)
        if value > limit and (before is None or value > before):
            was = f", {before} in the baseline" if before is not None else ""
            failures.append(f"FAIL  {value}  {key}  (limit {limit}{was})")
    notes = []
    for key, before in sorted(baseline.items()):
        if key not in scores:
            notes.append(f"note  {key} is gone: remove it from the baseline")
        elif scores[key] <= limit:
            notes.append(f"note  {key} is at {scores[key]} now, down from {before}: remove it from the baseline")
    return failures, notes


def over_limit(scores: dict[str, int], limit: int) -> dict[str, int]:
    return {key: value for key, value in sorted(scores.items()) if value > limit}


def visual_studio() -> Path | None:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        return None
    install = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                             capture_output=True, text=True).stdout.strip()
    return Path(install) if install else None


def find_clang_tidy(given: str | None, vs: Path | None) -> str | None:
    if given:
        return given if Path(given).is_file() else None
    if vs and (vs / "VC/Tools/Llvm/x64/bin/clang-tidy.exe").is_file():
        return str(vs / "VC/Tools/Llvm/x64/bin/clang-tidy.exe")
    return shutil.which("clang-tidy")


def configure(vs: Path, build: Path, vcpkg: Path) -> bool:
    ninja = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    toolchain = vcpkg / "scripts/buildsystems/vcpkg.cmake"
    if not toolchain.is_file():
        print(f"SKIP: no vcpkg toolchain at {toolchain} (pass --vcpkg-root, or run Generate-Solution.py first)")
        return False
    # Ninja, because only the Makefile and Ninja generators export a compile database. Precompiled
    # headers stay on so each command carries the build's /FI force-include, and score() strips the
    # /Yu and /Fp flags that would make clang look for an MSVC .pch.
    cmake = (f'cmake -S "{REPO}" -B "{build}" -G Ninja -DCMAKE_MAKE_PROGRAM="{ninja}" '
             f'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=OFF -DCMAKE_BUILD_TYPE=Debug '
             f'-DCMAKE_TOOLCHAIN_FILE="{toolchain}" -DVCPKG_INSTALLED_DIR="{vcpkg / "installed"}" '
             f'-DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF')
    vcvars = vs / "VC/Auxiliary/Build/vcvars64.bat"
    with tempfile.NamedTemporaryFile("w", suffix=".bat", delete=False) as bat:
        bat.write(f'@echo off\ncall "{vcvars}" >nul\n{cmake}\n')
    try:
        run = subprocess.run(["cmd", "/c", bat.name], capture_output=True, text=True)
    finally:
        os.unlink(bat.name)
    if run.returncode != 0 or not (build / "compile_commands.json").is_file():
        print(f"SKIP: the Ninja configure for the compile database failed:\n{(run.stdout + run.stderr)[-2000:]}")
        return False
    return True


def without_pch(entries: list[dict]) -> list[dict]:
    out = []
    for entry in entries:
        entry = dict(entry)
        entry["command"] = " ".join(t for t in entry["command"].split(" ") if not t.startswith(("/Yu", "/Fp", "/Yc")))
        out.append(entry)
    return out


def score(tidy: str, database: Path, source: str) -> tuple[list[tuple[str, str, int]], list[str], int]:
    header_filter = rf"{re.escape(REPO.as_posix())}/(Snowstorm-Core|Snowstorm-Editor|Snowstorm-Runtime|Games)/.*"
    run = subprocess.run([tidy, "-p", str(database), "--quiet", "-checks=-*,readability-function-cognitive-complexity",
                          f"-config={TIDY_CONFIG}", f"-header-filter={header_filter}", source],
                         capture_output=True, text=True)
    found = []
    own_errors = []
    foreign_errors = 0
    for line in (run.stdout + run.stderr).splitlines():
        line = line.strip()
        if match := WARNING.match(line):
            if is_scored(match.group(1)):
                found.append((rel(match.group(1)), match.group(3), int(match.group(4))))
        elif match := ERROR.match(line):
            if is_scored(match.group(1)):
                own_errors.append(f"{rel(match.group(1))}: {match.group(2)}")
            else:
                foreign_errors += 1
    return found, own_errors, foreign_errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build-tidy", help="Ninja configure directory")
    parser.add_argument("--vcpkg-root", type=Path, default=Path(os.environ.get("SS_VCPKG_ROOT", REPO / "vcpkg")))
    parser.add_argument("--clang-tidy", help="clang-tidy.exe, default the one Visual Studio ships")
    parser.add_argument("--list", action="store_true", help="print every scored function")
    parser.add_argument("--update-baseline", action="store_true", help="record the functions over the limit now")
    args = parser.parse_args()

    vs = visual_studio()
    tidy = find_clang_tidy(args.clang_tidy, vs)
    if tidy is None or vs is None:
        print("SKIP: no Visual Studio or no clang-tidy. Install the C++ Clang tools for Windows, or pass --clang-tidy.")
        return 2
    if not configure(vs, args.build_dir.resolve(), args.vcpkg_root.resolve()):
        return 2

    entries = json.loads((args.build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    sources = sorted({e["file"] for e in entries if is_scored(e["file"])})
    with tempfile.TemporaryDirectory() as temp:
        database = Path(temp)
        (database / "compile_commands.json").write_text(json.dumps(without_pch(entries)), encoding="utf-8")
        with ThreadPoolExecutor(max(1, (os.cpu_count() or 2) // 2)) as pool:
            results = list(pool.map(lambda source: score(tidy, database, source), sources))

    scores: dict[str, int] = {}
    own_errors: list[str] = []
    foreign_errors = 0
    for found, errors, foreign in results:
        own_errors += errors
        foreign_errors += foreign
        for path, function, value in found:
            key = f"{path}::{function}"
            scores[key] = max(value, scores.get(key, 0))

    if args.list:
        for key, value in sorted(scores.items(), key=lambda item: -item[1]):
            print(f"{value:4}  {key}")

    if args.update_baseline:
        current = over_limit(scores, LIMIT)
        BASELINE.write_text(json.dumps({"limit": LIMIT, "functions": current}, indent=2) + "\n", encoding="utf-8")
        print(f"baseline: {len(current)} functions over {LIMIT} written to {BASELINE.relative_to(REPO).as_posix()}")
        return 0

    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))["functions"] if BASELINE.is_file() else {}
    failures, notes = evaluate(scores, baseline, LIMIT)
    for line in notes + failures:
        print(line)
    for error in sorted(set(own_errors)):
        print(f"SKIP: clang could not parse {error}")
    if foreign_errors:
        print(f"note  {foreign_errors} errors inside third-party or STL headers (not the repository's code)")
    print(f"{len(sources)} files, {len(scores)} functions scored, {len(failures)} over the limit")
    if own_errors:
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
