#!/usr/bin/env python3
"""Check (or fix) clang-format cleanliness of the engine's C++ files.

Mirrors the CI `lint` workflow (.github/workflows/lint.yml), which enforces a
format-on-touch policy: only files a push/PR *changes* must be fully
clang-format-clean, so the codebase formats gradually. Run this before pushing
to catch the exact violations CI would flag.

Modes:
  (default)   Check files changed vs the merge-base with `master`, plus any
              uncommitted (staged + unstaged) changes. This is what CI gates on.
  --all       Check every tracked C++ file (the whole project). Reports the
              legacy backlog CI does NOT gate on -- informational.
  --fix       Reformat the selected files in place (clang-format -i) instead of
              just reporting. Combine with --all to format everything.

Excludes vendored `imgui_impl_*` files, matching CI. Uses whatever clang-format
is on PATH; CI pins 22.1.5, so match that locally to avoid version-drift diffs
(the workflow installs `clang-format==22.1.5`). Exit code is non-zero if any
checked file is not clean (in check mode).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

# clang-format diagnostic: "<path>:<line>:<col>: error: ...". The path may be a
# Windows absolute path (C:\...), so match the whole prefix, not split on ':'.
DIAG_RE = re.compile(r"^(.*?):\d+:\d+: error:")

ROOT = Path(__file__).resolve().parent.parent
EXTS = ("*.cpp", "*.hpp", "*.h")
EXCLUDE_SUBSTR = ("imgui_impl_",)
BASE_BRANCH = "master"


def run_git(*args):
    r = subprocess.run(["git", "-C", str(ROOT), *args],
                       capture_output=True, text=True)
    return r.stdout, r.returncode


def is_cpp(path):
    p = path.replace("\\", "/")
    if not p.endswith((".cpp", ".hpp", ".h")):
        return False
    return not any(sub in p for sub in EXCLUDE_SUBSTR)


def all_tracked_files():
    out, _ = run_git("ls-files", *EXTS)
    return [f for f in out.splitlines() if is_cpp(f)]


def changed_files():
    """Files changed vs the merge-base with master, plus uncommitted changes.

    Union of: committed-ahead (merge-base..HEAD), staged, and unstaged. This
    predicts what CI's `before...HEAD` range would flag for the next push.
    """
    files = set()

    base, rc = run_git("merge-base", BASE_BRANCH, "HEAD")
    base = base.strip()
    if rc == 0 and base:
        out, _ = run_git("diff", "--name-only", "--diff-filter=ACMR",
                         f"{base}...HEAD")
        files.update(out.splitlines())

    # Uncommitted: staged (--cached) and unstaged working-tree changes.
    out, _ = run_git("diff", "--name-only", "--diff-filter=ACMR", "--cached")
    files.update(out.splitlines())
    out, _ = run_git("diff", "--name-only", "--diff-filter=ACMR")
    files.update(out.splitlines())

    return [f for f in sorted(files) if is_cpp(f)]


# Windows caps a command line at ~32k chars; 386 absolute paths blow past it,
# so run clang-format over the file list in chunks.
CHUNK = 40


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def check(files, fix):
    if not files:
        print("No C++ files to check.")
        return 0

    abs_paths = [str(ROOT / f) for f in files]

    if fix:
        for group in chunks(abs_paths, CHUNK):
            subprocess.run(["clang-format", "-i", *group], check=True)
        print(f"Reformatted {len(files)} file(s) in place.")
        return 0

    # --dry-run --Werror exits non-zero and prints a diagnostic per dirty file.
    dirty = set()
    for group in chunks(abs_paths, CHUNK):
        r = subprocess.run(["clang-format", "--dry-run", "--Werror", *group],
                           capture_output=True, text=True)
        for line in r.stderr.splitlines():
            m = DIAG_RE.match(line)
            if m:
                dirty.add(m.group(1))
    dirty = sorted(dirty)

    print(f"Checked {len(files)} file(s).")
    if not dirty:
        print("All clean.")
        return 0

    print(f"\n{len(dirty)} file(s) need formatting:")
    for f in dirty:
        rel = Path(f)
        try:
            rel = rel.relative_to(ROOT)
        except ValueError:
            pass
        print(f"  {rel}")
    print("\nFix with:  py Scripts/check-format.py --fix")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true",
                    help="Check every tracked C++ file, not just changed ones.")
    ap.add_argument("--fix", action="store_true",
                    help="Reformat selected files in place instead of checking.")
    args = ap.parse_args()

    if subprocess.run(["clang-format", "--version"],
                      capture_output=True).returncode != 0:
        print("clang-format not found on PATH. Install clang-format==22.1.5 "
              "to match CI.")
        return 1

    files = all_tracked_files() if args.all else changed_files()
    scope = "whole project" if args.all else f"changed vs {BASE_BRANCH} + uncommitted"
    print(f"Scope: {scope}")
    return check(files, args.fix)


if __name__ == "__main__":
    sys.exit(main())
