"""Load the engine's exported super-resolution dataset (#46/#99/#102).

The engine's dataset.export writes, per frame along the scripted camera path:
    manifest.json    -> {format, frames:[{frame, jitter_ndc:[x,y], scale,
                                          lr:{...}, mv:{...}, gt:{...}, gt_ldr:{...}}]}
    frame_*_lr.npy     -> (sh, sw, 4) float16  low-res HDR color (unjittered by default, #102)
    frame_*_mv.npy     -> (h,  w,  4) float16  motion vectors (.xy)   [ignored: spatial model]
    frame_*_gt.npy     -> (h,  w,  4) float16  full-res HDR ground truth (linear)
    frame_*_gt_ldr.npy -> (h,  w,  4) uint8    full-res tonemapped LDR ground truth (sRGB bytes) (#102)

The spatial refiner trains LR (linear HDR input) -> gt_ldr (the engine's ACTUAL tonemapped present),
so the optimized target is byte-identical to what the in-engine PSNR/SSIM metric compares — closing
the train/inference tonemap-drift gap that made the earlier net (which trained against a Python ACES
approximation of the linear GT) lose to bilinear in-engine (#102). This loader yields random paired
crops: an LR patch and the aligned gt_ldr patch at the same scene location (gt_ldr patch = LR patch
upsampled by 1/scale). Cropping keeps training fast and gives many samples per frame.
"""

from __future__ import annotations

import json
import os
import random

import numpy as np
import torch
from torch.utils.data import Dataset


def _load_rgb(path: str) -> np.ndarray:
    """Load an (H,W,4) float16 HDR .npy as an (H,W,3) float32 array (drop alpha)."""
    a = np.load(path).astype(np.float32)
    return a[..., :3]


def _load_rgb_ldr(path: str) -> np.ndarray:
    """Load an (H,W,4) uint8 sRGB .npy as an (H,W,3) float32 array in [0,1] (drop alpha, /255)."""
    a = np.load(path).astype(np.float32) / 255.0
    return a[..., :3]


class SRCropDataset(Dataset):
    """Random aligned (LR crop, gt_ldr crop) pairs from an exported dataset directory.

    lr_crop is `crop` x `crop` linear-HDR input; gt_crop is `crop/scale` x `crop/scale` (the matching
    full-res region) of the engine's tonemapped LDR present, in [0,1]. Both returned CHW float32.
    `samples_per_frame` random crops are drawn from each captured frame. The dataset drops the black
    warmup frame(s) the engine emits before the first present is written (#102).
    """

    def __init__(self, root: str, crop: int = 64, samples_per_frame: int = 8, val_frac: float = 0.0):
        with open(os.path.join(root, "manifest.json")) as f:
            manifest = json.load(f)
        self.root = root
        frames = manifest["frames"]
        if not frames:
            raise ValueError(f"{root}: manifest has no frames")
        if "gt_ldr" not in frames[0]:
            raise ValueError(f"{root}: manifest has no 'gt_ldr' channel — recapture with the #102 build")
        self.crop = crop
        self.samples_per_frame = samples_per_frame

        # Decode every LR (HDR) + gt_ldr (sRGB) frame ONCE into RAM (dropping alpha). Re-loading the .npy on
        # every __getitem__ made data-loading the bottleneck (GPU near-idle); a few hundred frames fit easily.
        # Drop frames whose gt_ldr is all-black (the pre-first-present warmup the engine emits, #102) — they'd
        # teach the net to map real geometry to black. A gt_ldr crop mean < eps means nothing was presented yet.
        all_lr, all_gt, all_meta = [], [], []
        dropped = 0
        for f in frames:
            gt_ldr = _load_rgb_ldr(os.path.join(root, f["gt_ldr"]["file"]))
            if float(gt_ldr.max()) < 1e-4:
                dropped += 1
                continue
            all_lr.append(_load_rgb(os.path.join(root, f["lr"]["file"])))
            all_gt.append(gt_ldr)
            all_meta.append(f)
        if not all_lr:
            raise ValueError(f"{root}: every frame's gt_ldr was black — capture is broken")
        if dropped:
            print(f"dataset: dropped {dropped} black warmup frame(s)")

        # Held-out validation split: partition WHOLE frames (not crops) into train/val. Every `stride`-th
        # frame is validation, so val is spread across the whole camera path rather than one contiguous end
        # (a contiguous tail would all look alike and misrepresent generalization). Training crops are drawn
        # ONLY from train frames; model selection uses val full-frame PSNR (the fix for the #102 overfit).
        self._val_lr, self._val_gt = [], []
        self._lr, self._gt, self.frames = [], [], []
        stride = int(round(1.0 / val_frac)) if val_frac > 0.0 else 0
        for i, (lr, gt, meta) in enumerate(zip(all_lr, all_gt, all_meta)):
            if stride and i % stride == 0:
                self._val_lr.append(lr)
                self._val_gt.append(gt)
            else:
                self._lr.append(lr)
                self._gt.append(gt)
                self.frames.append(meta)
        if stride:
            print(f"dataset: {len(self._lr)} train / {len(self._val_lr)} val frames (stride {stride})")

        # scale is constant across a capture run; derive the LR->GT integer ratio (e.g. 0.5 -> 2).
        self.scale = float(frames[0]["scale"])
        self.ratio = int(round(1.0 / self.scale))
        assert self.ratio >= 1, f"bad scale {self.scale}"

    def val_frames(self):
        """The held-out (lr, gt_ldr) whole frames as HWC float32 arrays, for full-frame val PSNR. Empty
        if val_frac was 0."""
        return self._val_lr, self._val_gt

    def __len__(self) -> int:
        return len(self.frames) * self.samples_per_frame

    def __getitem__(self, idx: int):
        fi = idx // self.samples_per_frame
        lr = self._lr[fi]  # (sh, sw, 3) linear HDR, cached
        gt = self._gt[fi]  # (h, w, 3) sRGB LDR in [0,1], cached

        sh, sw, _ = lr.shape
        gh, gw, _ = gt.shape
        c = self.crop
        gc = c * self.ratio
        # Random LR crop origin, clamped so BOTH the LR patch and the aligned GT patch stay in bounds.
        # GT dims aren't always exactly ratio*LR (the engine rounds render.scale), so clamp the GT origin
        # to gt_dim - gc too, and derive the LR origin from it to keep them aligned.
        max_lx = max(0, min(sw - c, (gw - gc) // self.ratio))
        max_ly = max(0, min(sh - c, (gh - gc) // self.ratio))
        lx = random.randint(0, max_lx)
        ly = random.randint(0, max_ly)
        lr_crop = lr[ly:ly + c, lx:lx + c, :]

        gx, gy = lx * self.ratio, ly * self.ratio
        gt_crop = gt[gy:gy + gc, gx:gx + gc, :]

        # HWC -> CHW tensors.
        lr_t = torch.from_numpy(np.ascontiguousarray(lr_crop.transpose(2, 0, 1)))
        gt_t = torch.from_numpy(np.ascontiguousarray(gt_crop.transpose(2, 0, 1)))
        return lr_t, gt_t
