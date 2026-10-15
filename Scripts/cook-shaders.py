#!/usr/bin/env python3
"""Offline shader cook: compile Engine/Shaders/*.hlsl to SPIR-V headlessly (no GPU, no editor run).

A thin wrapper over Tools/ShaderCook, which builds the dxc command line with the engine's own
Snowstorm/Render/ShaderCompile.hpp, so the flags cannot drift from the runtime compile. Permutations
live in Scripts/shader-permutations.txt. The tool is built on first use (plain CMake, no vcpkg).

Usage (from anywhere):
    py Scripts/cook-shaders.py                 # cook the RT permutation of every shader -> cook dir
    py Scripts/cook-shaders.py --variants rt,base
    py Scripts/cook-shaders.py --out Engine/cache/shaders-cook --dxc Tools/dxc/dxc.exe
    py Scripts/cook-shaders.py --dry-run       # print the dxc command lines, run nothing

Exit 0 if every shader cooked, 1 if any failed.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL_SRC = ROOT / "Tools" / "ShaderCook"
TOOL_BUILD = ROOT / "build" / "shadercook"


def find_tool() -> "Path | None":
    for cand in (TOOL_BUILD / "Release" / "ShaderCook.exe", TOOL_BUILD / "ShaderCook.exe",
                 TOOL_BUILD / "Release" / "ShaderCook", TOOL_BUILD / "ShaderCook"):
        if cand.is_file():
            return cand
    return None


def build_tool() -> "Path | None":
    steps = [["cmake", "-S", str(TOOL_SRC), "-B", str(TOOL_BUILD)],
             ["cmake", "--build", str(TOOL_BUILD), "--config", "Release"]]
    for cmd in steps:
        if subprocess.run(cmd).returncode != 0:
            return None
    return find_tool()


def main() -> int:
    ap = argparse.ArgumentParser(description="Offline dxc shader cook for the RGA occupancy gate.")
    ap.add_argument("--dxc", default=str(ROOT / "Tools" / "dxc" / "dxc.exe"))
    ap.add_argument("--shaders", default="Engine/Shaders", help="Shader source dir")
    ap.add_argument("--out", default="Engine/cache/shaders-cook", help="Output SPIR-V dir")
    ap.add_argument("--variants", default="rt", help="Comma list of permutations: base,rt")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    tool = find_tool() or build_tool()
    if not tool:
        print("FAIL: could not build Tools/ShaderCook (needs cmake and a C++20 compiler)")
        return 1

    cmd = [str(tool), "--dxc", args.dxc, "--shaders", str(ROOT / args.shaders),
           "--out", str(ROOT / args.out), "--variants", args.variants,
           "--permutations", str(ROOT / "Scripts" / "shader-permutations.txt")]
    if args.dry_run:
        cmd.append("--dry-run")
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
