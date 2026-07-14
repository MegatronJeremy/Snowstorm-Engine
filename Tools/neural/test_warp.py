"""Parity tests for the Python MV backward-warp vs. the shader's intent (#98).

warp_history must match NeuralWarpHistory.comp.hlsl: sample prev at uv - velocity, bilinear, pixel-center
convention, out-of-frame -> 0, and a history_valid gate. These tests pin that behavior against
hand-computed oracles so the Python warp can't silently drift from the shader (which would reintroduce the
#102 train/inference mismatch, now in the temporal domain).
"""

from __future__ import annotations

import torch

from warp import build_features, warp_history


def _checker(h=4, w=4):
    """A distinct value per pixel so a shift is unambiguous: pixel (y,x) -> 10*y + x, broadcast to 3 ch."""
    vals = torch.arange(h * w, dtype=torch.float32).reshape(1, 1, h, w)
    return vals.repeat(1, 3, 1, 1)


def test_zero_velocity_is_identity():
    """velocity = 0 => histUv == uv => warp returns prev unchanged (bilinear at pixel centers)."""
    prev = _checker()
    vel = torch.zeros(1, 2, 4, 4)
    out = warp_history(prev, vel, history_valid=True)
    assert torch.allclose(out, prev, atol=1e-4), (out - prev).abs().max().item()


def test_integer_pixel_shift():
    """A one-pixel horizontal shift: velocity = +1 pixel in UV means histUv samples the pixel to the LEFT.

    velocity is curr_uv - prev_uv; setting velocity.x = +1/W shifts the sample one pixel left, so output
    column c == prev column c-1 (column 0 samples out of frame -> 0)."""
    prev = _checker(4, 4)
    vel = torch.zeros(1, 2, 4, 4)
    vel[:, 0] = 1.0 / 4  # +1 pixel in UV x
    out = warp_history(prev, vel, history_valid=True)
    # Expected: out[:, :, :, c] = prev[:, :, :, c-1]; c=0 -> 0 (disocclusion).
    expected = torch.zeros_like(prev)
    expected[:, :, :, 1:] = prev[:, :, :, :-1]
    assert torch.allclose(out, expected, atol=1e-4), (out - expected).abs().max().item()


def test_disocclusion_zeroes_out_of_frame():
    """A large velocity pushes every histUv outside [0,1] -> the whole warp is zero (padding_mode zeros)."""
    prev = _checker()
    vel = torch.full((1, 2, 4, 4), 5.0)  # way off-frame
    out = warp_history(prev, vel, history_valid=True)
    assert torch.count_nonzero(out) == 0


def test_history_invalid_returns_zero():
    """history_valid=False (first temporal frame) => all zeros, matching the shader's HistoryValid gate."""
    prev = _checker()
    vel = torch.zeros(1, 2, 4, 4)
    out = warp_history(prev, vel, history_valid=False)
    assert torch.count_nonzero(out) == 0


def test_half_pixel_bilinear():
    """A half-pixel shift bilinearly averages two neighbors — checks the interpolation, not just copies.

    velocity.x = +0.5/W shifts the sample half a pixel left; interior pixel c gets mean(prev[c-1], prev[c])."""
    prev = _checker(1, 4)  # single row: values 0,1,2,3
    vel = torch.zeros(1, 2, 1, 4)
    vel[:, 0] = 0.5 / 4  # +0.5 px in UV x
    out = warp_history(prev, vel, history_valid=True)
    # Interior columns 1..3: mean of left+self. Column 0 samples at uv=(0.5-0.5)/4 boundary -> clamp/zero mix,
    # so only assert the unambiguous interior.
    for c in range(1, 4):
        expected = 0.5 * (prev[0, 0, 0, c - 1] + prev[0, 0, 0, c])
        assert abs(out[0, 0, 0, c].item() - expected.item()) < 1e-4, (c, out[0, 0, 0, c].item(), expected.item())


def test_build_features_layout():
    """The 8-ch feature tensor is [bilinear(0..2), warped(3..5), velocity(6..7)] in that order."""
    bil = torch.ones(1, 3, 2, 2) * 0.1
    warped = torch.ones(1, 3, 2, 2) * 0.2
    vel = torch.ones(1, 2, 2, 2) * 0.3
    feat = build_features(bil, warped, vel)
    assert feat.shape == (1, 8, 2, 2)
    assert torch.allclose(feat[:, 0:3], bil) and torch.allclose(feat[:, 3:6], warped) and torch.allclose(feat[:, 6:8], vel)
