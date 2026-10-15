#!/usr/bin/env python3
"""Tests for quality-bench.py's optional LPIPS plumbing (no torch, no GPU).
Run directly (`py Scripts/test_quality_bench_lpips.py`) or under pytest."""
import importlib.util
from pathlib import Path

import numpy as np

_spec = importlib.util.spec_from_file_location("quality_bench", Path(__file__).resolve().parent / "quality-bench.py")
qb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qb)


def test_inputs_are_nchw_in_unit_range():
    a = np.zeros((4, 6, 4), dtype=np.uint8)
    b = np.full((4, 6, 4), 255, dtype=np.uint8)
    x, y = qb.lpips_inputs(a, b)
    assert x.shape == y.shape == (1, 3, 4, 6)
    assert x.dtype == np.float32
    assert float(x.max()) == -1.0 and float(y.min()) == 1.0


def test_alpha_is_dropped_and_channels_stay_ordered():
    img = np.zeros((2, 2, 4), dtype=np.uint8)
    img[..., 0] = 255
    x, _ = qb.lpips_inputs(img, img)
    assert float(x[0, 0].min()) == 1.0 and float(x[0, 1].max()) == -1.0


def test_score_is_none_without_torch(monkeypatch=None):
    qb._LPIPS_STATE.update(tried=True, fn=None)
    a = np.zeros((4, 4, 3), dtype=np.uint8)
    assert qb.lpips_score(a, a) is None


if __name__ == "__main__":
    for n, f in list(globals().items()):
        if n.startswith("test_"):
            f()
    print("ok")
