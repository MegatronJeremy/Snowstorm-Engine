#!/usr/bin/env python3
"""Tests for smoke-test.py's hang watchdog and device-lost marker (no GPU). Run directly or under pytest."""
import importlib.util
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location("smoke_test", Path(__file__).resolve().parent / "smoke-test.py")
st = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(st)

PY = sys.executable


def test_device_lost_is_an_error_marker():
    hits = st.scan_output("[info] ok\n[warning] vkQueueSubmit returned VK_ERROR_DEVICE_LOST\n", st.ERROR_MARKERS)
    assert len(hits) == 1


def test_clean_run_returns_output():
    rc, out, reason = st.run_with_watchdog([PY, "-c", "print('hello')"], ".", {}, 20, 10)
    assert rc == 0 and "hello" in out and reason is None


def test_silent_process_is_killed_on_stall():
    rc, out, reason = st.run_with_watchdog([PY, "-c", "import time; print('x', flush=True); time.sleep(60)"],
                                           ".", {}, 30, 2)
    assert reason and "no output" in reason


def test_wall_clock_timeout_kills():
    code = "import time\nwhile True:\n print('y', flush=True); time.sleep(0.2)"
    rc, out, reason = st.run_with_watchdog([PY, "-c", code], ".", {}, 2, 0)
    assert reason and "timed out" in reason


if __name__ == "__main__":
    for n, f in list(globals().items()):
        if n.startswith("test_"):
            f()
            print("ok", n)
