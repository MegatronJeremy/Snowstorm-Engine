"""Perceptual metric (LPIPS) for the neural upscaler (#98).

PSNR/SSIM structurally favor the BILINEAR baseline: bilinear(jittered LR) is a smooth, well-aligned blur
that scores high, and a net that injects real sub-pixel detail is punished by PSNR whenever that detail
lands even fractionally off the GT pixel grid. So a perceptually SHARPER result can score WORSE on PSNR.
This is exactly why DLSS/XeSS are evaluated on perceptual metrics (LPIPS) + human A/B, not PSNR. We keep
PSNR as a secondary readout but SELECT + report on LPIPS — the metric that matches the goal.

LPIPS (Zhang et al. 2018): distance in a pretrained-CNN feature space; LOWER = more perceptually similar
= better. Inputs are display-space [0,1] here; LPIPS wants [-1,1], so we remap.
"""

from __future__ import annotations

import torch

_lpips_net = None


def _net(device):
    """Lazily construct one shared LPIPS(alex) instance (loading the pretrained net is slow)."""
    global _lpips_net
    if _lpips_net is None:
        import lpips  # imported here so the rest of the pipeline doesn't hard-depend on it
        _lpips_net = lpips.LPIPS(net="alex", verbose=False).to(device).eval()
        for p in _lpips_net.parameters():
            p.requires_grad_(False)
    return _lpips_net


def lpips_distance(a_display: torch.Tensor, b_display: torch.Tensor) -> torch.Tensor:
    """LPIPS distance between two display-space [0,1] image batches (N,3,H,W). Lower = better. Returns a
    scalar (mean over the batch). Differentiable wrt `a_display`, so it can be a training loss."""
    net = _net(a_display.device)
    return net(a_display * 2.0 - 1.0, b_display * 2.0 - 1.0).mean()
