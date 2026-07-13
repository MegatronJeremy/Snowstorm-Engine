"""Train the spatial residual refiner on an exported dataset and write a .ssnn (#99).

Pipeline (mirrors the engine's inference exactly so trained weights mean the same in-engine):
    1. bilinear-upsample the LR crop to the GT crop's resolution (align_corners=False = pixel-center,
       matching NeuralUpsampleIn.comp.hlsl's SampleLevel).
    2. ResidualRefiner: output = upsampled + convStack(upsampled).
    3. L1 loss vs the GT crop; Adam.
    4. Log train PSNR each epoch alongside the BILINEAR baseline PSNR (residual=0) so you can see the
       net clear the bar. Save the best model to .ssnn.

Usage:
    python Tools/neural/train.py --dataset Dataset --out Assets/Neural/model.ssnn --epochs 200
"""

from __future__ import annotations

import argparse
import math
import os

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from dataset import SRCropDataset
from model import ResidualRefiner
from ssnn import save_ssnn
from tonemap import to_display


def psnr(mse: float) -> float:
    return 100.0 if mse <= 1e-12 else 10.0 * math.log10(1.0 / mse)


def upsample_to(lr: torch.Tensor, gt: torch.Tensor) -> torch.Tensor:
    """Bilinear-upsample LR to GT's spatial size; align_corners=False matches the shader's sampler."""
    return F.interpolate(lr, size=gt.shape[-2:], mode="bilinear", align_corners=False)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True, help="exported dataset dir (has manifest.json)")
    ap.add_argument("--out", required=True, help="output .ssnn path")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--crop", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--samples-per-frame", type=int, default=8)
    args = ap.parse_args()

    torch.manual_seed(0)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ds = SRCropDataset(args.dataset, crop=args.crop, samples_per_frame=args.samples_per_frame)
    dl = DataLoader(ds, batch_size=args.batch, shuffle=True, num_workers=0)
    print(f"device: {device}"
          f"{' (' + torch.cuda.get_device_name(0) + ')' if device.type == 'cuda' else ''}")
    print(f"dataset: {len(ds.frames)} frames, scale {ds.scale} (x{ds.ratio}), {len(ds)} crops/epoch")

    model = ResidualRefiner().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    best_psnr = -1.0
    for epoch in range(args.epochs):
        model.train()
        loss_sum = mse_sum = base_mse_sum = n = 0.0
        for lr_t, gt_t in dl:
            lr_t, gt_t = lr_t.to(device), gt_t.to(device)
            up = upsample_to(lr_t, gt_t)
            out = model(up)  # linear-HDR network output (what the engine writes, pre-tonemap)

            # Loss + PSNR are computed in the engine's DISPLAY space (ACES tonemap -> sRGB): the network
            # stays linear-HDR (the engine tonemaps AFTER it), but we optimize so its TONEMAPPED result
            # matches GT's tonemapped result — the same space the in-engine PSNR/SSIM metric measures. A
            # linear-HDR L1 gain otherwise becomes a post-ACES loss (they disagree).
            out_d, gt_d, up_d = to_display(out), to_display(gt_t), to_display(up)
            loss = F.l1_loss(out_d, gt_d)

            opt.zero_grad()
            loss.backward()
            opt.step()

            with torch.no_grad():
                mse_sum += F.mse_loss(out_d, gt_d).item() * lr_t.size(0)
                base_mse_sum += F.mse_loss(up_d, gt_d).item() * lr_t.size(0)  # bilinear baseline (display space)
                loss_sum += loss.item() * lr_t.size(0)
                n += lr_t.size(0)

        train_psnr = psnr(mse_sum / n)
        base_psnr = psnr(base_mse_sum / n)
        if (epoch + 1) % 10 == 0 or epoch == 0:
            print(f"epoch {epoch+1:4d}  L1 {loss_sum/n:.5f}  "
                  f"PSNR {train_psnr:6.2f} dB  (bilinear {base_psnr:6.2f} dB, "
                  f"gain {train_psnr - base_psnr:+.2f})")

        if train_psnr > best_psnr:
            best_psnr = train_psnr
            os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
            save_ssnn(args.out, model.to_layers())

    print(f"\nbest train PSNR {best_psnr:.2f} dB -> {args.out}")


if __name__ == "__main__":
    main()
