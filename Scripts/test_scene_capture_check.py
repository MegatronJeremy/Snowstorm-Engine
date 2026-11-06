#!/usr/bin/env python3
"""Tests for scene-capture-check.py's pure parts (no GPU): the error-frame detector and the PNG writer."""
import importlib.util
import tempfile
from pathlib import Path

import numpy as np

_spec = importlib.util.spec_from_file_location("scc", Path(__file__).resolve().parent / "scene-capture-check.py")
scc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(scc)

H, W = 288, 512


def _flat(rgb):
    return np.full((H, W, 3), rgb, dtype=np.uint8)


def _scene():
    """Sky gradient over a noisy grey floor with a few coloured blocks: a plausible real frame."""
    rng = np.random.default_rng(1)
    img = np.zeros((H, W, 3), dtype=np.float64)
    ramp = np.linspace(0, 1, H // 2)[:, None]
    img[: H // 2] = (np.array([60, 110, 200]) * (1 - ramp[..., None]) + np.array([250, 210, 160]) * ramp[..., None])
    img[H // 2:] = 90 + rng.normal(0, 25, (H - H // 2, W, 3))
    img[150:230, 100:160] = (230, 30, 30)
    img[150:230, 300:380] = (240, 240, 60)
    return np.clip(img, 0, 255).astype(np.uint8)


def test_magenta_frame_fails():
    assert "magenta" in scc.detect_error_frame(_flat((255, 0, 255)))


def test_the_owner_screenshot_shape_fails():
    img = np.zeros((H, W, 3), dtype=np.uint8)
    img[:] = (255, 0, 255)
    img[: H // 2] = (255, 170, 255)
    img[-40:] = (0, 0, 255)
    assert scc.detect_error_frame(img) is not None


def test_black_frame_fails():
    assert "black" in scc.detect_error_frame(_flat((2, 2, 2)))


def test_single_colour_frame_fails():
    assert "single colour" in scc.detect_error_frame(_flat((120, 140, 160)))


def test_real_scene_passes():
    assert scc.detect_error_frame(_scene()) is None


def test_dark_scene_with_a_lit_area_passes():
    img = np.full((H, W, 3), 3, dtype=np.uint8)
    img[60:260, 80:400] = _scene()[60:260, 80:400]
    assert scc.detect_error_frame(img) is None


def test_png_roundtrip_header():
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "a.png"
        scc.write_png(p, _scene())
        data = p.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n" and data.endswith(b"IEND\xaeB`\x82")


if __name__ == "__main__":
    for k, v in list(globals().items()):
        if k.startswith("test_"):
            v()
            print("ok", k)
