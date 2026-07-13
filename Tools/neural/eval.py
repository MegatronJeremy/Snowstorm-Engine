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


def psnr(mse: float) -> float:
    return 100.0 if mse <= 1e-12 else 10.0 * math.log10(1.0 / mse)


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

    base_mse = net_mse = 0.0
    n = 0
    for fr in frames:
        lr = np.load(os.path.join(args.dataset, fr["lr"]["file"])).astype(np.float32)[..., :3]
        gt = np.load(os.path.join(args.dataset, fr["gt_ldr"]["file"])).astype(np.float32)[..., :3] / 255.0
        if gt.max() < 1e-4:  # black warmup frame
            continue
        lr_t = torch.from_numpy(lr.transpose(2, 0, 1)).unsqueeze(0).to(device)
        gt_d = torch.from_numpy(gt.transpose(2, 0, 1)).unsqueeze(0).to(device)
        up = F.interpolate(lr_t, size=gt_d.shape[-2:], mode="bilinear", align_corners=False)
        with torch.no_grad():
            out = model(up)
            up_d, out_d = to_display(up), to_display(out)
            m = args.margin
            if m > 0:
                gt_d = gt_d[..., m:-m, m:-m]
                up_d = up_d[..., m:-m, m:-m]
                out_d = out_d[..., m:-m, m:-m]
            base_mse += F.mse_loss(up_d, gt_d).item()
            net_mse += F.mse_loss(out_d, gt_d).item()
        n += 1

    print(f"frames evaluated: {n}")
    print(f"bilinear full-frame PSNR: {psnr(base_mse / n):6.2f} dB")
    print(f"neural   full-frame PSNR: {psnr(net_mse / n):6.2f} dB")
    print(f"gain: {psnr(net_mse / n) - psnr(base_mse / n):+.2f} dB")


if __name__ == "__main__":
    main()
