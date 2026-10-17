#!/usr/bin/env python3
"""Offline BC1 round trip of one Sponza albedo texture, for docs/showcase/asset-pipeline.md.

Not the engine's encoder: the cook uses libsquish's iterative cluster fit. This is a numpy PCA-axis
fit with least-squares endpoint refinement, so it brackets the engine's quality from below.
Writes a side-by-side crop (source | BC1 | 4x error) and prints PSNR over the full texture.
"""
import argparse
import numpy as np
from PIL import Image


def to565(c):
    q = np.clip(np.rint(c / 255.0 * np.array([31, 63, 31])), 0, [31, 63, 31])
    return q, q / np.array([31, 63, 31]) * 255.0


def encode_bc1(img):
    h, w, _ = img.shape
    b = img.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3).astype(np.float64)
    mean = b.mean(1, keepdims=True)
    d = b - mean
    cov = np.einsum("nki,nkj->nij", d, d)
    axis = np.linalg.eigh(cov)[1][:, :, -1]
    t = np.einsum("nki,ni->nk", d, axis)
    lo = mean[:, 0] + axis * t.min(1)[:, None]
    hi = mean[:, 0] + axis * t.max(1)[:, None]
    w4 = np.array([0, 1 / 3, 2 / 3, 1.0])
    for _ in range(4):
        lo = to565(lo)[1]
        hi = to565(hi)[1]
        pal = lo[:, None, :] * (1 - w4)[None, :, None] + hi[:, None, :] * w4[None, :, None]
        idx = ((b[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1).argmin(-1)
        a = w4[idx]
        A = np.stack([1 - a, a], -1)
        AtA = np.einsum("nki,nkj->nij", A, A) + 1e-6 * np.eye(2)
        rhs = np.einsum("nki,nkc->nic", A, b)
        sol = np.linalg.solve(AtA, rhs)
        lo, hi = sol[:, 0], sol[:, 1]
    lo = to565(lo)[1]
    hi = to565(hi)[1]
    pal = lo[:, None, :] * (1 - w4)[None, :, None] + hi[:, None, :] * w4[None, :, None]
    idx = ((b[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1).argmin(-1)
    out = np.take_along_axis(pal, idx[:, :, None], 1)
    out = out.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3)
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


ap = argparse.ArgumentParser()
ap.add_argument("src")
ap.add_argument("out")
ap.add_argument("--crop", type=int, nargs=3, default=[384, 384, 128], metavar=("X", "Y", "SIZE"))
a = ap.parse_args()
src = np.asarray(Image.open(a.src).convert("RGB"))
dec = encode_bc1(src)
mse = ((src.astype(float) - dec) ** 2).mean()
print(f"PSNR full texture {10 * np.log10(255 ** 2 / mse):.2f} dB, {src.shape[1]}x{src.shape[0]}")
x, y, s = a.crop
err = np.clip(np.abs(src[y:y + s, x:x + s].astype(int) - dec[y:y + s, x:x + s]) * 4, 0, 255).astype(np.uint8)
panels = [src[y:y + s, x:x + s], dec[y:y + s, x:x + s], err]
sheet = np.concatenate([np.asarray(Image.fromarray(p).resize((s * 3, s * 3), Image.NEAREST)) for p in panels], 1)
Image.fromarray(sheet).save(a.out, quality=88)
