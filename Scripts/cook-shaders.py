#!/usr/bin/env python3
"""Offline shader cook: compile Engine/Shaders/*.hlsl to SPIR-V headlessly (no GPU, no editor run).

Mirrors the engine's runtime compile (Platform/Vulkan/VulkanShader.cpp CompileStageWithDxc): same
dxc, same flags, same profiles (vs/ps/cs_6_5, entry `main`), same permutation defines
(SS_RAYTRACING, SS_FP16). Its purpose is to populate a SPIR-V directory for Scripts/rga-occupancy.py
on a clean checkout or in CI, where booting the editor to fill Engine/cache/shaders/ is impossible
(no GPU). This is a deliberate second copy of the engine's dxc flags; keep it in sync with
VulkanShader.cpp when those flags change (there is no shared source of truth today).

Usage (from repo root or anywhere):
    py Scripts/cook-shaders.py                 # cook the RT permutation of every shader -> cook dir
    py Scripts/cook-shaders.py --variants rt,base
    py Scripts/cook-shaders.py --out Engine/cache/shaders-cook --dxc Tools/dxc/dxc.exe

Exit 0 if every shader cooked, 1 if any failed.
"""
import argparse
import subprocess
import sys
from pathlib import Path

STAGE_PROFILE = {"vert": "vs_6_5", "frag": "ps_6_5", "comp": "cs_6_5"}

# Permutation axes the engine injects at runtime (VulkanShader.cpp). "rt" is the worst case (heaviest
# register/occupancy), which is what the occupancy gate keys on; "base" is the ForceNonRT path.
VARIANTS = {"base": [], "rt": ["SS_RAYTRACING=1", "SS_FP16=1"]}

# Per-shader FEATURE permutations: axes a CALL SITE picks (ShaderDefines passed to ShaderLibrary::Load),
# unlike the device-capability axes above. The distinction matters to the occupancy gate: capability
# variants are alternatives on one device and collapse to the worst case, while feature variants run
# SIMULTANEOUSLY in one frame, so each is gated on its own instead of hiding behind the heaviest consumer.
# Only combinations a call site actually requests are listed; cooking the full cross product is how shader
# variant counts explode. Keep in sync with the call sites that pass defines to Load().
FEATURE_VARIANTS = {
    # DefaultLit's inline ray-query shadow path is a DYNAMIC axis (ShaderReloadSystem swaps it in place from
    # a frame-constant CVar) rather than a call-site one, but the gate still needs both compiled to see the
    # occupancy difference, and the heavier variant is what runs when inline RT shadows are on.
    "DefaultLit.frag": [
        ("inlineshadow", ["SS_LIT_INLINE_RT_SHADOWS=1"]), # shadows.mode=RayTraced without the stochastic pass
        ("noinlineshadow", []),                           # stochastic shadows / raster / off: traversal is dead
    ],
    # render.gi.restir selects a permutation at startup; the reservoir path is compiled out when it is off,
    # which is what keeps the default path at full occupancy.
    "GI.comp": [
        ("restir", ["SS_GI_RESTIR=1"]), # experimental ReSTIR path: reservoir state is live across the ray loop
        ("norestir", []),               # default: no reservoir code, no reservoir bindings
    ],
    "GIDenoise.comp": [
        ("gi", []),                            # GI + reflections: neither extra term
        ("ao", ["SS_DENOISE_HITDIST=1"]),      # AO: REBLUR-style hit-distance edge-stop
        ("shadow", ["SS_DENOISE_PENUMBRA=1"]), # shadows (diffuse + demodulated specular): SIGMA penumbra
    ],
}

# Exactly the flags CompileStageWithDxc passes (minus -T/-I/-Fo/-D, added per-invocation below).
BASE_FLAGS = ["-spirv", "-E", "main", "-fspv-target-env=vulkan1.2", "-fvk-use-dx-layout",
              "-Zpr", "-enable-16bit-types", "-fspv-preserve-bindings"]


def stage_for(src: Path) -> str:
    """'Reflection.comp.hlsl' -> 'comp'; stage-less 'IBLBRDFLut.hlsl' -> 'comp' (engine treats a
    single-path shader as compute)."""
    parts = src.name.split(".")
    if len(parts) >= 3 and parts[-2] in STAGE_PROFILE:
        return parts[-2]
    return "comp"


def main() -> int:
    ap = argparse.ArgumentParser(description="Offline dxc shader cook for the RGA occupancy gate.")
    ap.add_argument("--dxc", default=None, help="Path to dxc.exe (default Tools/dxc/dxc.exe)")
    ap.add_argument("--shaders", default="Engine/Shaders", help="Shader source dir")
    ap.add_argument("--out", default="Engine/cache/shaders-cook", help="Output SPIR-V dir")
    ap.add_argument("--variants", default="rt", help="Comma list of permutations: base,rt")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    dxc = Path(args.dxc) if args.dxc else root / "Tools" / "dxc" / "dxc.exe"
    shaders_dir = (root / args.shaders).resolve()
    out = (root / args.out).resolve()

    if not dxc.is_file():
        print(f"FAIL: dxc not found at {dxc}")
        return 1
    if not shaders_dir.is_dir():
        print(f"FAIL: shader dir not found at {shaders_dir}")
        return 1
    out.mkdir(parents=True, exist_ok=True)

    # Purge stale .spv before cooking. The cook regenerates everything it owns, so anything left over is
    # from a previous run with a different shader or variant list, and rga-occupancy.py would analyse it as
    # a live module: a renamed shader or a changed FEATURE_VARIANTS entry otherwise leaves a phantom that
    # --update-baseline bakes in permanently. Observed with DefaultLit.frag_{base,rt}.spv surviving the
    # switch to per-feature variants and reporting an entry the cook no longer produces.
    stale = list(out.glob("*.spv"))
    for f in stale:
        f.unlink()
    if stale:
        print(f"cleared {len(stale)} stale .spv from {out.name}")

    variants = [v for v in args.variants.split(",") if v in VARIANTS]
    if not variants:
        print(f"FAIL: no valid variants in '{args.variants}' (known: {list(VARIANTS)})")
        return 1

    srcs = sorted(shaders_dir.glob("*.hlsl"))
    print(f"dxc     : {dxc}")
    print(f"shaders : {shaders_dir}  ({len(srcs)} sources)")
    print(f"out     : {out}   variants: {variants}\n")

    ok = fail = 0
    for src in srcs:
        stage = stage_for(src)
        profile = STAGE_PROFILE[stage]
        # '__<feature>' marks a call-site permutation; the trailing '_<variant>' is the capability one.
        # rga-occupancy.py splits on exactly that, keeping the first and folding the second.
        for ftag, fdefs in FEATURE_VARIANTS.get(src.stem, [("", [])]):
            for vname in variants:
                suffix = f"__{ftag}" if ftag else ""
                out_spv = out / f"{src.stem}{suffix}_{vname}.spv"  # src.stem keeps the stage token
                cmd = [str(dxc), str(src), *BASE_FLAGS, "-T", profile,
                       "-I", str(shaders_dir), "-Fo", str(out_spv)]
                for d in (*fdefs, *VARIANTS[vname]):
                    cmd += ["-D", d]
                proc = subprocess.run(cmd, capture_output=True, text=True)
                if proc.returncode == 0 and out_spv.exists():
                    ok += 1
                else:
                    fail += 1
                    label = f"{ftag}/{vname}" if ftag else vname
                    print(f"  FAIL {src.name} [{label}] ({profile}): {proc.stderr.strip()[:180]}")

    print(f"\ncooked {ok} ok, {fail} failed -> {out.relative_to(root)}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
