"""Train the spatial residual refiner on an exported dataset and write a .ssnn (#99).

Pipeline (mirrors the engine's inference exactly so trained weights mean the same in-engine):
    1. bilinear-upsample the LR crop (linear HDR) to the GT crop's resolution (align_corners=False =
       pixel-center, matching NeuralUpsampleIn.comp.hlsl's SampleLevel).
    2. ResidualRefiner: output = upsampled + convStack(upsampled), still linear HDR.
    3. Tonemap the linear-HDR net output through the engine's transform (ACES -> sRGB), then L1 it
       against the REAL tonemapped LDR ground truth (gt_ldr, the engine's actual present bytes, #102).
       Only the forward tonemap is Python now; the TARGET is the engine's true output, so 'beats
       bilinear' means the same thing in and out of engine (closes the #102 tonemap-drift gap).
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


def full_frame_psnr(model, lr_frames, gt_frames, device) -> tuple[float, float]:
    """Neural + bilinear full-frame display-space PSNR over a set of whole frames (the HELD-OUT
    validation set). This is the honest metric — the in-engine PSNR/SSIM measures whole frames, and
    training-crop PSNR is measured on the training set itself, so it overstates the gain. Model
    selection MUST use this, not train-crop PSNR (that was the #102 overfitting trap)."""
    model.eval()
    base_mse = net_mse = 0.0
    with torch.no_grad():
        for lr, gt in zip(lr_frames, gt_frames):
            lr_t = torch.from_numpy(lr.transpose(2, 0, 1)).unsqueeze(0).to(device)
            gt_d = torch.from_numpy(gt.transpose(2, 0, 1)).unsqueeze(0).to(device)
            up = F.interpolate(lr_t, size=gt_d.shape[-2:], mode="bilinear", align_corners=False)
            up_d, out_d = to_display(up), to_display(model(up))
            base_mse += F.mse_loss(up_d, gt_d).item()
            net_mse += F.mse_loss(out_d, gt_d).item()
    n = max(1, len(lr_frames))
    return psnr(net_mse / n), psnr(base_mse / n)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True, help="exported dataset dir (has manifest.json)")
    ap.add_argument("--out", required=True, help="output .ssnn path")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--crop", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--samples-per-frame", type=int, default=8)
    ap.add_argument("--val-frac", type=float, default=0.2, help="fraction of frames held out for validation")
    ap.add_argument("--weight-decay", type=float, default=1e-4, help="Adam L2 regularization (curbs overfit)")
    args = ap.parse_args()

    torch.manual_seed(0)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ds = SRCropDataset(args.dataset, crop=args.crop, samples_per_frame=args.samples_per_frame,
                       val_frac=args.val_frac)
    dl = DataLoader(ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_lr, val_gt = ds.val_frames()
    print(f"device: {device}"
          f"{' (' + torch.cuda.get_device_name(0) + ')' if device.type == 'cuda' else ''}")
    print(f"dataset: {len(ds.frames)} train frames, scale {ds.scale} (x{ds.ratio}), {len(ds)} crops/epoch")

    model = ResidualRefiner().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    # Model selection is on HELD-OUT full-frame val PSNR, not train-crop PSNR. Train-crop PSNR is measured
    # on the training set and overstates the gain — selecting on it produced a net that scored +1.5 dB on
    # crops but LOST -0.85 dB on full frames both in Python and in-engine (the #102 overfit). The saved
    # .ssnn is the epoch with the best val gain over bilinear; if no epoch beats bilinear on val, the run
    # HONESTLY reports that instead of shipping a worse-than-bilinear model.
    if not val_lr:
        raise SystemExit("need --val-frac > 0 for honest model selection")
    best_val_gain = -1e9
    best_epoch = -1
    for epoch in range(args.epochs):
        model.train()
        loss_sum = n = 0.0
        for lr_t, gt_d in dl:
            lr_t, gt_d = lr_t.to(device), gt_d.to(device)
            up = upsample_to(lr_t, gt_d)  # size only; gt_d is the full-res LDR reference
            out = model(up)  # linear-HDR network output (what the engine writes, pre-tonemap)

            # Loss is computed in the engine's DISPLAY space (ACES tonemap -> sRGB). The network stays
            # linear-HDR (the engine tonemaps AFTER it), so we tonemap its output HERE and compare to gt_d
            # — the REAL engine LDR present (gt_ldr), the exact bytes the in-engine PSNR/SSIM metric scores.
            out_d = to_display(out)
            loss = F.l1_loss(out_d, gt_d)

            opt.zero_grad()
            loss.backward()
            opt.step()
            loss_sum += loss.item() * lr_t.size(0)
            n += lr_t.size(0)

        val_psnr, val_base = full_frame_psnr(model, val_lr, val_gt, device)
        val_gain = val_psnr - val_base
        if (epoch + 1) % 10 == 0 or epoch == 0:
            print(f"epoch {epoch+1:4d}  L1 {loss_sum/n:.5f}  "
                  f"val PSNR {val_psnr:6.2f} dB  (bilinear {val_base:6.2f} dB, gain {val_gain:+.2f})")

        if val_gain > best_val_gain:
            best_val_gain = val_gain
            best_epoch = epoch + 1
            os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
            save_ssnn(args.out, model.to_layers())

    verdict = "BEATS bilinear" if best_val_gain > 0 else "does NOT beat bilinear"
    print(f"\nbest val gain {best_val_gain:+.2f} dB @ epoch {best_epoch} ({verdict}) -> {args.out}")


if __name__ == "__main__":
    main()
