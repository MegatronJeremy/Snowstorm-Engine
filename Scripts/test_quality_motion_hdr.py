#!/usr/bin/env python3
"""Tests for quality-motion.py's HDR-FLIP plumbing (#157; no GPU, no flip-evaluator needed: the metrics are
stubbed). Run directly or under pytest."""
import importlib.util
import tempfile
from pathlib import Path

import numpy as np

_spec = importlib.util.spec_from_file_location("quality_motion", Path(__file__).resolve().parent / "quality-motion.py")
qm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qm)
qb = qm.qb


def _stub():
    saved = {k: getattr(qb, k) for k in ("flip", "psnr", "ssim", "flip_hdr")}
    qb.flip = lambda a, b: 0.1
    qb.psnr = lambda a, b: 30.0
    qb.ssim = lambda a, b: 0.9
    qb.flip_hdr = lambda a, b: (float(np.abs(a - b).mean()), {}, 0)
    saved_cvvdp = qm.temporal_flip
    qm.temporal_flip = lambda *a: 0.01
    return saved, saved_cvvdp


def _unstub(state):
    saved, tf = state
    for k, v in saved.items():
        setattr(qb, k, v)
    qm.temporal_flip = tf


def _frames():
    qm.PROBES = {"p": (10, 11)}
    return {f: np.zeros((qb.CANON_H, qb.CANON_W, 3)) for f in (10, 11)}


def test_load_motion_hdr_reads_per_frame_files_and_skips_missing():
    d = Path(tempfile.mkdtemp())
    np.save(d / "motion_raster_f000010_hdr.npy", np.ones((9, 16, 4), dtype=np.float16))
    out = qm.load_motion_hdr(d / "motion_raster", [10, 11])
    assert list(out) == [10]
    assert out[10].shape == (qb.CANON_H, qb.CANON_W, 4) and out[10].dtype == np.float32


def test_load_motion_hdr_without_files_is_empty():
    assert qm.load_motion_hdr(Path(tempfile.mkdtemp()) / "motion_x", [1, 2]) == {}


def test_reference_hdr_sits_beside_the_reference():
    p = qm.motion_reference_hdr_path(Path("c/motion-f000010__abc.npy"))
    assert p == Path("c/motion-f000010__abc_hdr.npy")


def test_evaluate_reports_hdr_flip_per_frame_and_mean():
    st = _stub()
    try:
        imgs = _frames()
        hdr_i = {f: np.full((4, 4, 4), 2.0, np.float32) for f in (10, 11)}
        hdr_r = {10: np.full((4, 4, 4), 1.0, np.float32), 11: np.full((4, 4, 4), 0.0, np.float32)}
        r = qm.evaluate(imgs, imgs, ["p"], None, hdr_i, hdr_r)
        assert [row["flipHdr"] for row in r["perFrame"]] == [1.0, 2.0]
        assert r["flipHdr"] == 1.5
    finally:
        _unstub(st)


def test_evaluate_without_hdr_is_none_and_other_metrics_unchanged():
    st = _stub()
    try:
        imgs = _frames()
        r = qm.evaluate(imgs, imgs, ["p"])
        assert r["flipHdr"] is None and r["flip"] == 0.1 and r["psnr"] == 30.0
        assert all(row["flipHdr"] is None for row in r["perFrame"])
    finally:
        _unstub(st)


def test_evaluate_skips_hdr_pair_with_mismatched_shape():
    st = _stub()
    try:
        imgs = _frames()
        hdr_i = {10: np.zeros((4, 4, 4), np.float32)}
        hdr_r = {10: np.zeros((8, 8, 4), np.float32)}
        assert qm.evaluate(imgs, imgs, ["p"], None, hdr_i, hdr_r)["flipHdr"] is None
    finally:
        _unstub(st)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
