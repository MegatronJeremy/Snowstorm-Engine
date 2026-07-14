"""Reproduce the engine's output transform so training optimizes the SAME space the in-engine metric
measures (#99).

The engine's present pipeline (Tonemap.frag.hlsl + sRGB-format present target, #53/#79) is:
    ldr_linear = ACES(hdr * exposure)          # Narkowicz ACES filmic fit, saturated to [0,1]
    display    = linear_to_srgb(ldr_linear)    # hardware sRGB-encode on write
and the PSNR/SSIM metric (#45) compares those sRGB display bytes. The dataset exports LINEAR HDR, so we
apply this transform to LR and GT before the loss — otherwise the net optimizes linear-HDR error while
the metric scores tonemapped-sRGB error (they disagree: a linear-HDR gain can become a post-ACES loss).

Kept as plain tensor ops (differentiable) so it can sit inside the training graph.
"""

from __future__ import annotations

import torch

# ACES filmic (Narkowicz) constants — must match Tonemap.frag.hlsl exactly.
_A, _B, _C, _D, _E = 2.51, 0.03, 2.43, 0.59, 0.14


def aces(x: torch.Tensor) -> torch.Tensor:
    """Narkowicz ACES filmic tonemap, clamped to [0,1] (matches TonemapACES)."""
    return torch.clamp((x * (_A * x + _B)) / (x * (_C * x + _D) + _E), 0.0, 1.0)


def linear_to_srgb(c: torch.Tensor) -> torch.Tensor:
    """sRGB OETF (the hardware encode on write to the sRGB present target). Input/output in [0,1]."""
    c = torch.clamp(c, 0.0, 1.0)
    lo = c * 12.92
    hi = 1.055 * torch.pow(c.clamp(min=1e-8), 1.0 / 2.4) - 0.055
    return torch.where(c <= 0.0031308, lo, hi)


def to_display(hdr: torch.Tensor, exposure: float = 1.0) -> torch.Tensor:
    """Full engine output transform: linear HDR -> ACES(exposure) -> sRGB display, the space the metric
    (and the viewer) sees. Train against this so 'beats bilinear' means the same thing in and out of engine."""
    return linear_to_srgb(aces(hdr * exposure))
