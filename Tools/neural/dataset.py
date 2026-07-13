"""Load the engine's exported super-resolution dataset (#46/#99).

The engine's dataset.export writes, per frame along the scripted camera path:
    manifest.json  -> {format, frames:[{frame, jitter_ndc:[x,y], scale,
                                        lr:{file,w,h,c,dtype}, mv:{...}, gt:{...}}]}
    frame_*_lr.npy -> (sh, sw, 4) float16  jittered low-res HDR color
    frame_*_mv.npy -> (h,  w,  4) float16  motion vectors (.xy)   [ignored: spatial model]
    frame_*_gt.npy -> (h,  w,  4) float16  full-res unjittered HDR ground truth

The spatial refiner (#99 first pass) trains on LR->GT only. This loader yields random paired crops:
an LR patch and the aligned GT patch at the same scene location (GT patch = LR patch upsampled by
1/scale). Cropping keeps CPU training on the tiny net fast and gives many samples per frame.
"""

from __future__ import annotations

import json
import os
import random

import numpy as np
import torch
from torch.utils.data import Dataset


def _load_rgb(path: str) -> np.ndarray:
    """Load an (H,W,4) float16 .npy as an (H,W,3) float32 array (drop alpha)."""
    a = np.load(path).astype(np.float32)
    return a[..., :3]


class SRCropDataset(Dataset):
    """Random aligned (LR crop, GT crop) pairs from an exported dataset directory.

    lr_crop is `crop` x `crop`; gt_crop is `crop/scale` x `crop/scale` (the matching full-res region).
    Both returned CHW float32. `samples_per_frame` random crops are drawn from each captured frame.
    """

    def __init__(self, root: str, crop: int = 64, samples_per_frame: int = 8):
        with open(os.path.join(root, "manifest.json")) as f:
            manifest = json.load(f)
        self.root = root
        self.frames = manifest["frames"]
        self.crop = crop
        self.samples_per_frame = samples_per_frame
        if not self.frames:
            raise ValueError(f"{root}: manifest has no frames")
        # scale is constant across a capture run; derive the LR->GT integer ratio (e.g. 0.5 -> 2).
        self.scale = float(self.frames[0]["scale"])
        self.ratio = int(round(1.0 / self.scale))
        assert self.ratio >= 1, f"bad scale {self.scale}"

    def __len__(self) -> int:
        return len(self.frames) * self.samples_per_frame

    def __getitem__(self, idx: int):
        frame = self.frames[idx // self.samples_per_frame]
        lr = _load_rgb(os.path.join(self.root, frame["lr"]["file"]))  # (sh, sw, 3)
        gt = _load_rgb(os.path.join(self.root, frame["gt"]["file"]))  # (h, w, 3)

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
