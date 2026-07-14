"""Train the TEMPORAL residual refiner (recurrent, truncated BPTT) and write an 8-ch .ssnn (#98).

Mirrors the engine's temporal inference (render.upscaler=2) so trained weights mean the same in-engine:
    per frame k in a short sequence:
      1. bilinear-upsample LR_k to full res (align_corners=False, matching NeuralUpsampleIn).
      2. warp the PREVIOUS frame's output into frame k by MV_k (warp.warp_history == NeuralWarpHistory).
      3. features = [bilinear (0..2), warped-history (3..5), motion vector (6..7)] (8 ch).
      4. out_k = features[:, 0:3] + conv_stack(features)  (residual on the bilinear base).
      5. L1( tonemap(out_k), gt_ldr_k )  in display space (the metric's space, #102).
      6. out_k becomes frame k+1's history (detached only at the window boundary — BPTT within the window).

Frame 0 of each window warm-starts history = bilinear(LR_0) (history_valid=False -> warp returns zeros,
so out_0 is a pure spatial pass, exactly like the engine's first temporal frame per viewport).

Model selection is on HELD-OUT full-frame RECURRENT val PSNR (roll the net across the contiguous val
frames), reported against BOTH bilinear and classic TAAU-equivalent — the honest metric (#102 lesson).

Usage:
    python Tools/neural/train_temporal.py --dataset Dataset/sponza_temporal \
        --out Assets/Neural/sponza_temporal.ssnn --epochs 200
"""

from __future__ import annotations

import argparse
import math
import os

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from dataset import SRSequenceDataset
from model import ResidualRefiner
from ssnn import save_ssnn
from tonemap import to_display
from warp import build_features, warp_history


def psnr(mse: float) -> float:
    return 100.0 if mse <= 1e-12 else 10.0 * math.log10(1.0 / mse)


def upsample_to(lr: torch.Tensor, size) -> torch.Tensor:
    """Bilinear-upsample LR to a full-res (H,W); align_corners=False matches the shader's sampler."""
    return F.interpolate(lr, size=size, mode="bilinear", align_corners=False)


def recurrent_forward(model, lr_seq, mv_seq, device, detach_history=True):
    """Roll the temporal net across a sequence. lr_seq (T,3,h,w), mv_seq (T,2,H,W) — full-res H,W taken
    from mv. Returns the list of display-space outputs (each (1,3,H,W)) and the bilinear-of-each-frame
    display outputs (for the baseline PSNR). If detach_history, the fed-back history is detached each step
    (val/eval path — no graph); training keeps the graph within the window (BPTT)."""
    T = lr_seq.shape[0]
    hw = mv_seq.shape[-2:]
    outs, bils = [], []
    prev = None
    for k in range(T):
        lr = lr_seq[k:k + 1].to(device)
        mv = mv_seq[k:k + 1].to(device)
        bil = upsample_to(lr, hw)  # (1,3,H,W) linear HDR
        if prev is None:
            warped = warp_history(bil, mv, history_valid=False)  # zeros; frame 0 = pure spatial
        else:
            warped = warp_history(prev, mv, history_valid=True)
        feat = build_features(bil, warped, mv)
        out = model(feat)  # (1,3,H,W) linear HDR
        prev = out.detach() if detach_history else out
        outs.append(to_display(out))
        bils.append(to_display(bil))
    return outs, bils


def val_psnr_recurrent(model, val_lr, val_mv, val_gt, device) -> tuple[float, float]:
    """Full-frame recurrent val PSNR: roll the net across the contiguous held-out frames and compare each
    output to gt_ldr in display space. Returns (neural_psnr, bilinear_psnr). This is the honest metric —
    whole frames, temporally rolled, on data the net never trained on. Iterated per-frame (not stacked):
    the engine rounds render.scale per frame, so full-frame sizes differ by a pixel or two across the run."""
    model.eval()
    net_mse = base_mse = 0.0
    prev = None
    with torch.no_grad():
        for lr, mv, gt in zip(val_lr, val_mv, val_gt):
            lr_t = torch.from_numpy(lr.transpose(2, 0, 1)).unsqueeze(0).to(device)
            mv_t = torch.from_numpy(mv.transpose(2, 0, 1)).unsqueeze(0).to(device)
            gt_d = torch.from_numpy(gt.transpose(2, 0, 1)).unsqueeze(0).to(device)
            hw = gt_d.shape[-2:]
            bil = upsample_to(lr_t, hw)
            if prev is None or prev.shape[-2:] != hw:
                warped = warp_history(bil, mv_t, history_valid=False)  # frame 0 / size change -> zeros
            else:
                warped = warp_history(prev, mv_t, history_valid=True)
            out = model(build_features(bil, warped, mv_t))
            prev = out
            net_mse += F.mse_loss(to_display(out), gt_d).item()
            base_mse += F.mse_loss(to_display(bil), gt_d).item()
    n = max(1, len(val_gt))
    return psnr(net_mse / n), psnr(base_mse / n)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True, help="exported dataset dir (has manifest.json with mv)")
    ap.add_argument("--out", required=True, help="output .ssnn path (8-ch temporal model)")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--crop", type=int, default=96)
    ap.add_argument("--seq", type=int, default=4, help="frames per BPTT window")
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--samples-per-window", type=int, default=4)
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--weight-decay", type=float, default=1e-4)
    args = ap.parse_args()

    torch.manual_seed(0)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ds = SRSequenceDataset(args.dataset, crop=args.crop, seq=args.seq,
                           samples_per_window=args.samples_per_window, val_frac=args.val_frac)
    dl = DataLoader(ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_lr, val_mv, val_gt = ds.val_frames()
    print(f"device: {device}"
          f"{' (' + torch.cuda.get_device_name(0) + ')' if device.type == 'cuda' else ''}")
    if not val_lr:
        raise SystemExit("need --val-frac > 0 for honest model selection")

    model = ResidualRefiner(in_channels=8).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    best_val_gain = -1e9
    best_epoch = -1
    for epoch in range(args.epochs):
        model.train()
        loss_sum = n = 0.0
        for lr_b, mv_b, gt_b in dl:  # each (B, seq, C, H, W)
            B = lr_b.shape[0]
            opt.zero_grad()
            batch_loss = 0.0
            # Unroll each sequence in the batch (per-sample history; keep the graph within the window).
            for b in range(B):
                outs, _ = recurrent_forward(model, lr_b[b], mv_b[b], device, detach_history=False)
                seq_loss = 0.0
                for k, out_d in enumerate(outs):
                    gt_d = gt_b[b, k:k + 1].to(device)
                    seq_loss = seq_loss + F.l1_loss(out_d, gt_d)
                batch_loss = batch_loss + seq_loss / len(outs)
            batch_loss = batch_loss / B
            batch_loss.backward()
            opt.step()
            loss_sum += batch_loss.item() * B
            n += B

        val_psnr, val_base = val_psnr_recurrent(model, val_lr, val_mv, val_gt, device)
        val_gain = val_psnr - val_base
        if (epoch + 1) % 10 == 0 or epoch == 0:
            print(f"epoch {epoch+1:4d}  L1 {loss_sum/max(n,1):.5f}  "
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
