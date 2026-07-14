"""Offline full-frame eval: does the trained .ssnn beat bilinear on the SAME data, in Python? (#102)

This isolates MODEL quality from ENGINE inference. It runs the exact training forward pass (bilinear
upsample -> residual refiner -> engine tonemap) on WHOLE frames (not crops) and reports full-frame
PSNR vs the real gt_ldr, for both bilinear and neural. Compare to the in-engine metric:

    Python neural > bilinear  AND  engine neural < bilinear  => engine-inference mismatch (the bug).
    Python neural < bilinear                                 => model/distribution problem (retrain).

Usage:
    python Tools/neural/eval.py --dataset Dataset/sponza_v2 --weights Assets/Neural/sponza.ssnn
"""

from __future__ import annotations

import argparse
import json
import math
import os

import numpy as np
import torch
import torch.nn.functional as F

from model import ResidualRefiner
from ssnn import load_ssnn
from tonemap import to_display
from warp import build_features, warp_history


def psnr(mse: float) -> float:
    return 100.0 if mse <= 1e-12 else 10.0 * math.log10(1.0 / mse)


def _load_lr(dataset, fr):
    return np.load(os.path.join(dataset, fr["lr"]["file"])).astype(np.float32)[..., :3]


def _load_gt(dataset, fr):
    return np.load(os.path.join(dataset, fr["gt_ldr"]["file"])).astype(np.float32)[..., :3] / 255.0


def _load_mv(dataset, fr):
    return np.load(os.path.join(dataset, fr["mv"]["file"])).astype(np.float32)[..., :2]


def eval_spatial(model, dataset, frames, device, margin):
    """Single-frame full-frame PSNR (#47/#102 path). Each frame independent."""
    base_mse = net_mse = 0.0
    n = 0
    for fr in frames:
        gt = _load_gt(dataset, fr)
        if gt.max() < 1e-4:  # black warmup frame
            continue
        lr_t = torch.from_numpy(_load_lr(dataset, fr).transpose(2, 0, 1)).unsqueeze(0).to(device)
        gt_d = torch.from_numpy(gt.transpose(2, 0, 1)).unsqueeze(0).to(device)
        up = F.interpolate(lr_t, size=gt_d.shape[-2:], mode="bilinear", align_corners=False)
        with torch.no_grad():
            up_d, out_d = to_display(up), to_display(model(up))
            if margin > 0:
                gt_d, up_d, out_d = gt_d[..., margin:-margin, margin:-margin], up_d[..., margin:-margin, margin:-margin], out_d[..., margin:-margin, margin:-margin]
            base_mse += F.mse_loss(up_d, gt_d).item()
            net_mse += F.mse_loss(out_d, gt_d).item()
        n += 1
    return net_mse, base_mse, n


def eval_temporal(model, dataset, frames, device, margin):
    """Recurrent full-frame PSNR (#98 path): roll the net across the ORDERED frames, feeding each output
    back as the next frame's MV-warped history — exactly like the engine's render.upscaler=2. Frame 0
    warm-starts with zero history (pure spatial), matching the first temporal frame per viewport."""
    base_mse = net_mse = 0.0
    n = 0
    prev = None
    for fr in frames:
        gt = _load_gt(dataset, fr)
        if gt.max() < 1e-4:  # black warmup frame -> reset the temporal chain
            prev = None
            continue
        lr_t = torch.from_numpy(_load_lr(dataset, fr).transpose(2, 0, 1)).unsqueeze(0).to(device)
        mv_t = torch.from_numpy(_load_mv(dataset, fr).transpose(2, 0, 1)).unsqueeze(0).to(device)
        gt_d = torch.from_numpy(gt.transpose(2, 0, 1)).unsqueeze(0).to(device)
        hw = gt_d.shape[-2:]
        up = F.interpolate(lr_t, size=hw, mode="bilinear", align_corners=False)
        with torch.no_grad():
            # A per-frame render.scale rounding change (or the first frame) makes prev's size != this frame:
            # treat it as disocclusion (zero history), matching the engine's resize-invalidates-history guard.
            if prev is not None and prev.shape[-2:] == hw:
                warped = warp_history(prev, mv_t, history_valid=True)
            else:
                warped = warp_history(up, mv_t, history_valid=False)
            feat = build_features(up, warped, mv_t)
            out = model(feat)
            prev = out
            up_d, out_d = to_display(up), to_display(out)
            if margin > 0:
                gt_d, up_d, out_d = gt_d[..., margin:-margin, margin:-margin], up_d[..., margin:-margin, margin:-margin], out_d[..., margin:-margin, margin:-margin]
            base_mse += F.mse_loss(up_d, gt_d).item()
            net_mse += F.mse_loss(out_d, gt_d).item()
        n += 1
    return net_mse, base_mse, n


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--weights", required=True)
    ap.add_argument("--margin", type=int, default=0,
                    help="exclude this many border px from the PSNR (tests conv-padding border effects)")
    args = ap.parse_args()

    with open(os.path.join(args.dataset, "manifest.json")) as f:
        frames = json.load(f)["frames"]

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = ResidualRefiner.from_layers(load_ssnn(args.weights)).to(device).eval()
    temporal = model.in_channels == 8  # auto-detect from the loaded model's first-layer width
    print(f"model: {model.in_channels}-ch ({'TEMPORAL recurrent' if temporal else 'spatial single-frame'})")

    if temporal:
        net_mse, base_mse, n = eval_temporal(model, args.dataset, frames, device, args.margin)
    else:
        net_mse, base_mse, n = eval_spatial(model, args.dataset, frames, device, args.margin)

    print(f"frames evaluated: {n}")
    print(f"bilinear full-frame PSNR: {psnr(base_mse / n):6.2f} dB")
    print(f"neural   full-frame PSNR: {psnr(net_mse / n):6.2f} dB")
    print(f"gain: {psnr(net_mse / n) - psnr(base_mse / n):+.2f} dB")


if __name__ == "__main__":
    main()
