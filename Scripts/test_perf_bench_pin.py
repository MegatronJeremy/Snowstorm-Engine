#!/usr/bin/env python3
"""Unit tests for the pure parts of perf-bench.py's --pin-clocks (no GPU, no nvidia-smi).
Run directly (`py Scripts/test_perf_bench_pin.py`, exit 0/1) or under pytest."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace as NS

_spec = importlib.util.spec_from_file_location("perf_bench", Path(__file__).resolve().parent / "perf-bench.py")
pb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pb)

ROWS = "0, NVIDIA GeForce RTX 5070, 3090\n1, NVIDIA GeForce RTX 3060, 2100\nnoise line\n"


def test_parse_skips_garbage():
    rows = pb.parse_nvidia_query(ROWS)
    assert [(r["index"], r["maxMhz"]) for r in rows] == [(0, 3090), (1, 2100)]


def test_select_by_index_name_and_ambiguity():
    rows = pb.parse_nvidia_query(ROWS)
    assert pb.select_nvidia_gpu(rows, "1")["name"].endswith("3060")
    assert pb.select_nvidia_gpu(rows, "5070")["index"] == 0
    assert pb.select_nvidia_gpu(rows, "") is None          # two adapters, no choice made
    assert pb.select_nvidia_gpu(rows, "geforce") is None   # matches both
    assert pb.select_nvidia_gpu(rows[:1], "")["index"] == 0
    assert pb.select_nvidia_gpu(rows, "9070") is None


def test_pin_cmds():
    lock, restore = pb.nvidia_pin_cmds(1, 1800)
    assert lock == ["nvidia-smi", "-i", "1", "--lock-gpu-clocks=1800,1800"]
    assert restore[-1] == "--reset-gpu-clocks"


def test_clockpin_no_nvidia_is_unpinned_with_reason():
    p = pb.ClockPin("", which=lambda _: None)
    assert not p.pinned and "UNPINNED" in p.note


def test_clockpin_locks_and_restores():
    calls = []

    def run(cmd, **kw):
        calls.append(cmd)
        return NS(returncode=0, stdout=ROWS, stderr="")

    p = pb.ClockPin("5070", which=lambda _: "x", run=run)
    assert p.pinned and p.mhz == 3090
    assert calls[-1] == ["nvidia-smi", "-i", "0", "--lock-gpu-clocks=3090,3090"]
    p.restore()
    assert calls[-1][-1] == "--reset-gpu-clocks"
    n = len(calls)
    p.restore()                                            # idempotent
    assert len(calls) == n


def test_clockpin_lock_refused():
    def run(cmd, **kw):
        if any("lock-gpu-clocks" in c for c in cmd):
            return NS(returncode=1, stdout="", stderr="Insufficient Permissions")
        return NS(returncode=0, stdout=ROWS, stderr="")

    p = pb.ClockPin("5070", which=lambda _: "x", run=run)
    assert not p.pinned and "elevated" in p.note


if __name__ == "__main__":
    fails = 0
    for k, f in list(globals().items()):
        if k.startswith("test_"):
            try:
                f()
                print("ok  ", k)
            except AssertionError as e:
                fails += 1
                print("FAIL", k, e)
    raise SystemExit(1 if fails else 0)
