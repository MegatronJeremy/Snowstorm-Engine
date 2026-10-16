#!/usr/bin/env python3
"""Tests for quality-bench.py's HDR-FLIP plumbing (#157; no GPU). Needs flip-evaluator for the metric tests,
which skip when it is not installed. Run directly or under pytest."""
import importlib.util
from pathlib import Path

import numpy as np

_spec = importlib.util.spec_from_file_location("quality_bench", Path(__file__).resolve().parent / "quality-bench.py")
qb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qb)


def _has_flip():
    try:
        import flip_evaluator  # noqa: F401
        return True
    except Exception:
        return False


def _scene(seed=0):
    rng = np.random.default_rng(seed)
    img = rng.random((32, 48, 4), dtype=np.float32) * 4.0  # radiance above 1: the range LDR clips
    img[..., 3] = 1.0
    return img


def test_sanitize_counts_and_zeroes_non_finite_and_negative():
    img = _scene()
    img[0, 0, 0] = np.nan
    img[1, 1, 1] = np.inf
    img[2, 2, 2] = -1.0
    rgb, bad = qb.sanitize_hdr(img)
    assert bad == 2 and rgb.shape == (32, 48, 3)
    assert np.isfinite(rgb).all() and float(rgb.min()) == 0.0


def test_sanitize_drops_alpha_and_keeps_float32():
    rgb, bad = qb.sanitize_hdr(_scene().astype(np.float16))
    assert rgb.dtype == np.float32 and rgb.shape[-1] == 3 and bad == 0


def test_load_hdr_missing_is_none(tmp_path=None):
    assert qb.load_hdr(Path("does-not-exist") / "x") is None


def test_load_hdr_resizes_to_canonical(tmp_path=None):
    import tempfile
    d = Path(tempfile.mkdtemp())
    np.save(d / "cap_hdr.npy", np.ones((9, 16, 4), dtype=np.float16))
    out = qb.load_hdr(d / "cap")
    assert out.shape == (qb.CANON_H, qb.CANON_W, 4) and out.dtype == np.float32


def test_identical_images_score_zero():
    if not _has_flip():
        return
    img = _scene()
    mean, params, bad = qb.flip_hdr(img, img)
    assert mean == 0.0 and bad == 0 and "startExposure" in params and "stopExposure" in params


def test_brighter_test_image_scores_nonzero_and_more_error_scores_higher():
    if not _has_flip():
        return
    ref = _scene()
    slight, _, _ = qb.flip_hdr(ref, np.concatenate([ref[..., :3] * 1.1, ref[..., 3:]], -1))
    big, _, _ = qb.flip_hdr(ref, np.concatenate([ref[..., :3] * 2.0, ref[..., 3:]], -1))
    assert 0.0 < slight < big


def test_nan_is_reported_not_fed_to_flip():
    if not _has_flip():
        return
    ref = _scene()
    bad_img = ref.copy()
    bad_img[3, 3, 0] = np.nan
    mean, _, bad = qb.flip_hdr(ref, bad_img)
    assert bad == 1 and np.isfinite(mean)


def test_highlight_error_invisible_to_ldr_is_seen_by_hdr():
    if not _has_flip():
        return
    ref = np.full((32, 48, 4), 0.5, dtype=np.float32)
    ref[8:24, 12:36, :3] = 50.0  # far above the tonemap shoulder
    test = ref.copy()
    test[8:24, 12:36, :3] = 25.0  # half the radiance, both saturate to white in LDR
    mean, _, _ = qb.flip_hdr(ref, test)
    assert mean > 0.0


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
