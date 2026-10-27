#!/usr/bin/env python3
"""Tests for hdr-flip-noise.py (#157; no GPU). The end-to-end test needs flip-evaluator and skips without it."""
import importlib.util
import tempfile
from pathlib import Path

import numpy as np
import pytest

_spec = importlib.util.spec_from_file_location("hdr_flip_noise", Path(__file__).resolve().parent / "hdr-flip-noise.py")
hfn = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hfn)


def test_spread_stats_values():
    s = hfn.spread_stats([0.010, 0.012, 0.014])
    assert s["n"] == 3
    assert s["mean"] == pytest.approx(0.012)
    assert s["stdev"] == pytest.approx(0.002)
    assert s["range"] == pytest.approx(0.004)
    assert s["relRange"] == pytest.approx(0.004 / 0.012)


def test_spread_stats_single_value_has_zero_spread():
    s = hfn.spread_stats([0.5])
    assert s["stdev"] == 0.0 and s["range"] == 0.0


def test_spread_stats_zero_mean_does_not_divide():
    assert hfn.spread_stats([0.0, 0.0])["relRange"] == 0.0


def test_spread_stats_rejects_empty():
    with pytest.raises(ValueError):
        hfn.spread_stats([])


def test_one_repeat_is_a_skip_not_a_pass(tmp_path):
    img = np.ones((8, 8, 4), dtype=np.float32)
    np.save(tmp_path / "ref_hdr.npy", img)
    np.save(tmp_path / "a_hdr.npy", img)
    rc = hfn.main(["--ref", str(tmp_path / "ref"), "--runs", str(tmp_path / "a")])
    assert rc == 2


def test_missing_reference_fails(tmp_path):
    assert hfn.main(["--ref", str(tmp_path / "nope"), "--runs", str(tmp_path / "a"), str(tmp_path / "b")]) == 1


def test_end_to_end_spread(tmp_path):
    pytest.importorskip("flip_evaluator")
    rng = np.random.default_rng(1)
    ref = rng.random((64, 96, 4), dtype=np.float32) * 4.0
    np.save(tmp_path / "ref_hdr.npy", ref)
    for i in range(3):
        noisy = ref * (1.0 + 0.02 * (i + 1) * rng.standard_normal(ref.shape).astype(np.float32))
        np.save(tmp_path / f"r{i}_hdr.npy", noisy.astype(np.float32))
    runs = [str(tmp_path / f"r{i}") for i in range(3)]
    assert hfn.main(["--ref", str(tmp_path / "ref"), "--runs", *runs]) == 0
