#!/usr/bin/env python3
"""Unit tests for the gate logic in shader-stats.py (pure: no GPU, no build)."""
import copy
import importlib.util
import json
from pathlib import Path

_dir = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("shader_stats", _dir / "shader-stats.py")
ss = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ss)


def exe(pipeline, stage, **stats):
    return {"pipeline": pipeline, "stages": stage, "stats": {k: {"format": "uint64", "value": v} for k, v in stats.items()}}


def amd(*rows):
    return {"device": "AMD Radeon RX 9070 XT", "source": "t", "executables": [
        exe(p, st, numUsedVgprs=r, scratchMemUsageInBytes=sp) for p, st, r, sp in rows]}


def nv(*rows):
    return {"device": "NVIDIA GeForce RTX 5070", "source": "t", "executables": [
        exe(p, st, **{"Register Count": r, "Local Memory Size": 0x10_00_000000 + sp}) for p, st, r, sp in rows]}


def test_identical_passes():
    d = amd(("A.comp", "compute", 64, 0), ("B.comp", "compute", 100, 0))
    fails, _, n = ss.gate(d, copy.deepcopy(d))
    assert fails == [] and n == 2


def test_amd_occupancy_drop_fails_but_same_wave_rise_passes():
    base = amd(("A.comp", "compute", 80, 0), ("B.comp", "compute", 100, 0))
    cur = amd(("A.comp", "compute", 96, 0), ("B.comp", "compute", 150, 0))  # A: 16->16, B: 14->9
    fails, _, _ = ss.gate(base, cur)
    assert len(fails) == 1 and "B.comp" in fails[0] and "occupancy" in fails[0]


def test_amd_spill_appearing_is_hard_fail():
    fails, _, _ = ss.gate(amd(("A.comp", "compute", 40, 0)), amd(("A.comp", "compute", 40, 272)))
    assert len(fails) == 1 and "spill" in fails[0]


def test_existing_spill_is_not_a_new_failure():
    fails, _, _ = ss.gate(amd(("A.comp", "compute", 40, 272)), amd(("A.comp", "compute", 40, 272)))
    assert fails == []


def test_nvidia_register_rise_needs_percent_and_absolute():
    base = nv(("A.comp", "compute", 40, 0), ("B.comp", "compute", 100, 0), ("C.comp", "compute", 60, 16))
    cur = nv(("A.comp", "compute", 46, 0), ("B.comp", "compute", 120, 0), ("C.comp", "compute", 60, 16))
    fails, notes, _ = ss.gate(base, cur)  # A +6 (<8 abs), B +20%
    assert len(fails) == 1 and "B.comp" in fails[0]


def test_nvidia_withheld_spill_column_never_gates():
    base = nv(("A.comp", "compute", 40, 0), ("B.comp", "compute", 40, 16))
    cur = nv(("A.comp", "compute", 40, 48), ("B.comp", "compute", 40, 16))
    fails, notes, _ = ss.gate(base, cur)
    assert fails == [] and any("withheld" in n for n in notes)


def test_device_mismatch_compares_nothing():
    fails, notes, n = ss.gate(amd(("A.comp", "compute", 40, 0)), nv(("A.comp", "compute", 40, 0)))
    assert n == 0 and fails == [] and notes


def test_coverage_drift_is_noted_not_failed():
    base = amd(("A.comp", "compute", 40, 0), ("B.comp", "compute", 40, 0))
    cur = amd(("A.comp", "compute", 40, 0), ("C.comp", "compute", 40, 0))
    fails, notes, n = ss.gate(base, cur)
    assert fails == [] and n == 1 and len(notes) == 2


def test_permutations_pair_by_sorted_registers():
    base = amd(("D.frag", "fragment", 127, 0), ("D.frag", "fragment", 87, 0))
    cur = amd(("D.frag", "fragment", 87, 0), ("D.frag", "fragment", 127, 0))  # order differs only
    fails, _, n = ss.gate(base, cur)
    assert fails == [] and n == 2


def test_committed_baselines_gate_against_themselves():
    for p in sorted((_dir / "shader-stats-baseline").glob("*.json")):
        d = json.loads(p.read_text(encoding="utf-8"))
        fails, _, n = ss.gate(d, copy.deepcopy(d))
        assert fails == [] and n == len(d["executables"]), p.name


if __name__ == "__main__":
    import sys
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except AssertionError:
                failed += 1
                print(f"FAIL {name}")
    sys.exit(1 if failed else 0)
