#!/usr/bin/env python3
"""Unit tests for smoke-test.py's --exe/--cwd resolution (no GPU, no process launched).
Run directly (`py Scripts/test_smoke_test_exe.py`, exit 0/1) or under pytest."""
import importlib.util
import sys
from pathlib import Path
from unittest import mock

_spec = importlib.util.spec_from_file_location("smoke_test", Path(__file__).resolve().parent / "smoke-test.py")
st = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(st)

BASE = Path("/games/doom").resolve()


def test_relative_exe_and_default_cwd():
    name, exe, cwd = st.external_target("build/Debug/Snowstorm-Doom.exe", None, BASE)
    assert name == "Snowstorm-Doom"
    assert exe == BASE / "build/Debug/Snowstorm-Doom.exe"
    assert cwd == BASE


def test_cwd_relative_and_absolute():
    assert st.external_target("a.exe", "run", BASE)[2] == BASE / "run"
    abs_cwd = str(Path("/elsewhere").resolve())
    assert st.external_target("a.exe", abs_cwd, BASE)[2] == Path(abs_cwd)


def test_absolute_exe_kept():
    abs_exe = Path("/x/y/Game.exe").resolve()
    assert st.external_target(str(abs_exe), None, BASE)[1] == abs_exe


def _main(argv):
    with mock.patch.object(sys, "argv", ["smoke-test.py"] + argv):
        return st.main()


def test_exe_rejects_only_staged_and_orphan_cwd():
    assert _main(["--exe", "g.exe", "--only", "Runtime"]) == 1
    assert _main(["--exe", "g.exe", "--staged"]) == 1
    assert _main(["--cwd", "."]) == 1


def test_exe_runs_one_target_through_run_target():
    calls = []

    def fake(name, exe, cwd, frames, timeout, *rest):
        calls.append((name, exe, cwd, frames, timeout))
        return True

    with mock.patch.object(st, "run_target", fake):
        assert _main(["--exe", "/g/Game.exe", "--cwd", ".", "--frames", "7"]) == 0
    assert calls == [("Game", Path("/g/Game.exe").resolve(), Path.cwd(), 7, st.DEFAULT_TIMEOUT_OTHER)]


def test_exe_failure_propagates():
    with mock.patch.object(st, "run_target", lambda *a, **k: False):
        assert _main(["--exe", "/g/Game.exe"]) == 1


if __name__ == "__main__":
    failed = 0
    for n, f in sorted(globals().items()):
        if n.startswith("test_") and callable(f):
            try:
                f()
            except AssertionError as e:
                failed += 1
                print(f"FAIL {n}: {e!r}")
    sys.exit(1 if failed else 0)
