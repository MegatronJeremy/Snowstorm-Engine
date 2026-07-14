"""Motion-vector backward warp — the Python twin of NeuralWarpHistory.comp.hlsl (#98).

The engine's temporal upscaler reprojects the previous frame's output into the current frame by sampling
it at `histUv = uv - velocity` (bilinear, clamp-to-edge, disocclusion -> 0). Training must apply the
EXACT same warp so the net sees identical inputs in training and inference — no train/inference drift
(the #102 lesson). This module is that warp in torch, and test_warp.py pins it against a hand-computed
oracle so it can't silently diverge from the shader.

Conventions (must match the shader + VelocityPass):
  - velocity = curr_uv - prev_uv (points current -> previous), in top-left UV space (V=0 at top).
  - uv of pixel (x,y) = (x+0.5, y+0.5) / (W,H)  — pixel centers.
  - histUv = uv - velocity; sample prev at histUv, bilinear, disocclusion (out of [0,1]) -> 0.
The shader uses a clamp-to-edge sampler but zeroes out-of-bounds explicitly (the `inBounds` guard), so
the net effect is padding_mode='zeros'. grid_sample(align_corners=False) matches SampleLevel's pixel
centering.
"""

from __future__ import annotations

import torch
import torch.nn.functional as F


def warp_history(prev: torch.Tensor, velocity: torch.Tensor, history_valid: bool = True) -> torch.Tensor:
    """Backward-warp `prev` (N,3,H,W) by `velocity` (N,2,H,W, = curr_uv - prev_uv, UV space).

    Returns (N,3,H,W): prev sampled at uv - velocity, bilinear, out-of-frame -> 0. history_valid=False
    (first temporal frame / after resize) returns all zeros, matching the shader's HistoryValid gate."""
    n, _, h, w = prev.shape
    if not history_valid:
        return torch.zeros_like(prev)

    # Pixel-center UV grid, matching the shader's (id.xy + 0.5)/size.
    ys = (torch.arange(h, device=prev.device, dtype=prev.dtype) + 0.5) / h
    xs = (torch.arange(w, device=prev.device, dtype=prev.dtype) + 0.5) / w
    grid_y, grid_x = torch.meshgrid(ys, xs, indexing="ij")  # (H,W) each
    uv = torch.stack((grid_x, grid_y), dim=-1)  # (H,W,2) in [0,1]
    uv = uv.unsqueeze(0).expand(n, -1, -1, -1)  # (N,H,W,2)

    # histUv = uv - velocity. velocity is (N,2,H,W) -> (N,H,W,2).
    vel = velocity.permute(0, 2, 3, 1)
    hist_uv = uv - vel  # (N,H,W,2), UV in [0,1] nominal

    # grid_sample expects normalized coords in [-1,1] (x,y), align_corners=False => same pixel-center
    # convention as the shader's SampleLevel. padding_mode='zeros' reproduces the disocclusion -> 0 guard.
    grid = hist_uv * 2.0 - 1.0
    return F.grid_sample(prev, grid, mode="bilinear", padding_mode="zeros", align_corners=False)


def build_features(bilinear_rgb: torch.Tensor, warped_hist: torch.Tensor, velocity: torch.Tensor) -> torch.Tensor:
    """Assemble the 8-channel temporal feature tensor the way NeuralUpscalePass fills its feature buffer:
    [bilinear RGB (0..2), warped-history RGB (3..5), motion vector xy (6..7)]. All (N,·,H,W)."""
    return torch.cat((bilinear_rgb, warped_hist, velocity), dim=1)
