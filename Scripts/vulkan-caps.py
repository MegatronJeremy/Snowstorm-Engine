#!/usr/bin/env python3
"""Report what each GPU's driver offers for cooperative matrix, fp16 and subgroup size control.

    py Scripts/vulkan-caps.py                       # default adapter
    py Scripts/vulkan-caps.py --gpu 9060 --gpu 8060 # one capture per adapter, one table
    py Scripts/vulkan-caps.py --json a.json b.json  # format existing captures, no GPU run

Boots Snowstorm-Runtime for one frame with `vulkan.caps` set; the engine queries the physical device
(VulkanDeviceCaps) and writes JSON. No extension is enabled for the query, so the table is what the driver
advertises, not what the engine uses. Needs a real GPU, so it is local. Exit 0 with all captures read, 1 on a
failed capture, 2 when --json names nothing.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")


def shapes_table(doc: dict) -> list[str]:
    """One line per cooperative-matrix shape, sorted so two adapters diff cleanly."""
    shapes = doc.get("cooperativeMatrix", {}).get("shapes", [])
    rows = sorted(shapes, key=lambda s: (s["scope"], s["A"], s["B"], s["C"], s["Result"], s["M"], s["N"], s["K"]))
    return [f"{s['M']}x{s['N']}x{s['K']}  A={s['A']} B={s['B']} C={s['C']} R={s['Result']}  {s['scope']}"
            f"{'  saturating' if s.get('saturatingAccumulation') else ''}" for s in rows]


def has_f16_shape(doc: dict) -> bool:
    """True when some subgroup-scope shape multiplies f16 inputs (the NeuralConv port's precondition)."""
    return any(s["A"] == "f16" and s["B"] == "f16" and s["scope"] == "subgroup"
               for s in doc.get("cooperativeMatrix", {}).get("shapes", []))


def summarize(doc: dict) -> list[str]:
    ext = doc.get("extensions", {})
    f16 = doc.get("float16Int8", {})
    sg = doc.get("subgroup", {})
    cm = doc.get("cooperativeMatrix", {})
    out = [
        f"{doc.get('device', '?')}  driver {doc.get('driverName', '?')} {doc.get('driverInfo', '')}  Vulkan {doc.get('apiVersion', '?')}",
        f"  VK_KHR_cooperative_matrix   ext {ext.get('VK_KHR_cooperative_matrix')}  feature {cm.get('supported')}"
        f"  stages {cm.get('supportedStages', [])}",
        f"  VK_KHR_shader_float16_int8  ext {ext.get('VK_KHR_shader_float16_int8')}  shaderFloat16 {f16.get('shaderFloat16')}"
        f"  shaderInt8 {f16.get('shaderInt8')}  16-bit storage {f16.get('storageBuffer16BitAccess')}",
        f"  subgroup_size_control       ext {ext.get('VK_EXT_subgroup_size_control')}  size {sg.get('size')}"
        f"  range {sg.get('minSize')}..{sg.get('maxSize')}  control {sg.get('subgroupSizeControl')}"
        f"  required-size stages {sg.get('requiredSizeStages', [])}",
        f"  f16 x f16 subgroup shape: {'yes' if has_f16_shape(doc) else 'NO'}",
    ]
    out += ["    " + line for line in shapes_table(doc)]
    return out


def capture(repo_root: Path, exe: Path, gpu: str, timeout: int) -> dict | None:
    out = repo_root / "Engine" / "cache" / f"vulkan-caps-{gpu or 'auto'}.json"
    out.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update({"SS_VULKAN_CAPS": "1", "SS_VULKAN_CAPS_PATH": str(out), "SS_SMOKE_FRAMES": "1",
                "SS_CONFIG_IGNORE": "1", "SS_HEADLESS": "1"})
    if gpu:
        env["SS_RENDER_GPU"] = gpu
    layers = repo_root / "vcpkg" / "installed" / "x64-windows" / "bin"
    if layers.is_dir():
        env["VK_ADD_LAYER_PATH"] = str(layers)
    proc = subprocess.run([str(exe)], cwd=str(repo_root), env=env, capture_output=True, text=True, timeout=timeout)
    if not out.is_file():
        print(f"  no capture for gpu '{gpu or 'auto'}' (exit {proc.returncode}); tail of output:")
        print("\n".join(proc.stdout.splitlines()[-8:]))
        return None
    return json.loads(out.read_text(encoding="utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gpu", action="append", default=[], help="adapter name substring or index; repeatable")
    ap.add_argument("--json", nargs="+", help="format existing captures instead of running")
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()

    docs = []
    if args.json:
        docs = [json.loads(Path(p).read_text(encoding="utf-8")) for p in args.json]
    else:
        repo_root = Path(__file__).resolve().parent.parent
        exe = repo_root / "build" / "Snowstorm-Runtime" / args.config / "Snowstorm-Runtime.exe"
        if not exe.is_file():
            print(f"FAIL: {exe} not found. Build first.")
            return 1
        for g in args.gpu or [""]:
            d = capture(repo_root, exe, g, args.timeout)
            if d is None:
                return 1
            docs.append(d)
    if not docs:
        return 2
    for d in docs:
        print("\n".join(summarize(d)))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
