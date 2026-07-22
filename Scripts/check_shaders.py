#!/usr/bin/env python3
"""Validate that the engine's HLSL shaders compile to SPIR-V with dxc.

Mirrors the runtime split in VulkanShader.cpp: each .hlsl under Engine/Shaders is
split on '#type vertex' / '#type fragment' markers (lines before the first marker are
shared and prepended to every stage), entry point 'main', compiled vs_6_0 / ps_6_0.

Usable locally (py Scripts/check_shaders.py) and in CI. Windows-only — uses the
bundled Tools/dxc/dxc.exe. Exit code is non-zero if any stage fails to compile.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DXC = ROOT / "Tools" / "dxc" / "dxc.exe"
SHADER_DIR = ROOT / "Engine" / "Shaders"

STAGE_PROFILE = {"vertex": "vs_6_0", "fragment": "ps_6_0"}


def split_by_type(text):
    """Returns {stage: full_source} for each stage present in the file."""
    shared, sections, cur = [], {"vertex": [], "fragment": []}, None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#type"):
            cur = "vertex" if "vertex" in s else "fragment" if "fragment" in s else None
            continue
        (shared if cur is None else sections[cur]).append(line)
    preamble = "\n".join(shared)
    return {stage: preamble + "\n" + "\n".join(body) for stage, body in sections.items() if body}


def compile_stage(source, profile, label):
    # Write the temp file into SHADER_DIR so relative #includes resolve.
    with tempfile.NamedTemporaryFile("w", suffix=".hlsl", dir=SHADER_DIR,
                                     delete=False, encoding="utf-8") as f:
        f.write(source)
        tmp = Path(f.name)
    out = tmp.with_suffix(".spv")
    cmd = [
        str(DXC), "-spirv", "-E", "main", "-T", profile,
        "-fspv-target-env=vulkan1.2", "-fvk-use-dx-layout", "-Zpr",
        "-I", str(SHADER_DIR), "-Fo", str(out), str(tmp),
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  FAIL [{label}] {profile}")
            print((r.stdout + r.stderr).strip())
            return False
        print(f"  ok   [{label}] {profile}")
        return True
    finally:
        tmp.unlink(missing_ok=True)
        out.unlink(missing_ok=True)


def main():
    if not DXC.exists():
        print(f"dxc not found at {DXC}")
        return 1

    shaders = sorted(SHADER_DIR.glob("*.hlsl"))
    if not shaders:
        print("No .hlsl shaders found.")
        return 0

    ok = True
    for shader in shaders:
        print(f"{shader.name}:")
        stages = split_by_type(shader.read_text(encoding="utf-8"))
        if not stages:
            print("  (no #type sections, skipping)")
            continue
        for stage, source in stages.items():
            if not compile_stage(source, STAGE_PROFILE[stage], shader.stem):
                ok = False

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
