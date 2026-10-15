#!/usr/bin/env python3
"""Plan-level test for the shader cook: the dry run enumerates every source x feature x capability
variant, with the engine's flags and the file names rga-occupancy.py parses. Needs no dxc."""
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def plan():
    r = subprocess.run([sys.executable, str(ROOT / "Scripts" / "cook-shaders.py"), "--dry-run",
                        "--variants", "rt,base"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return [l for l in r.stdout.splitlines() if ".spv:" in l]


class CookPlan(unittest.TestCase):
    def test_count_matches_sources_and_permutations(self):
        sources = len(list((ROOT / "Engine" / "Shaders").glob("*.hlsl")))
        lines = [l for l in (ROOT / "Scripts" / "shader-permutations.txt").read_text().splitlines()
                 if l.strip() and not l.lstrip().startswith("#")]
        per_shader = {}
        for l in lines:
            per_shader[l.split("|")[0].strip()] = per_shader.get(l.split("|")[0].strip(), 0) + 1
        extra = sum(n - 1 for n in per_shader.values())
        self.assertEqual(len(plan()), (sources + extra) * 2)

    def test_names_and_flags(self):
        for l in plan():
            name = l.split(":")[0]
            self.assertRegex(name, r"^[A-Za-z0-9.]+(__[a-z]+)?_(rt|base)\.spv$")
            self.assertIn("-fspv-preserve-bindings", l)
            self.assertEqual("SS_RAYTRACING=1" in l, name.endswith("_rt.spv"))

    def test_feature_defines_reach_the_command(self):
        got = {l.split(":")[0]: l for l in plan()}
        self.assertIn("-D SS_GI_RESTIR=1", got["GI.comp__restir_rt.spv"])
        self.assertNotIn("SS_GI_RESTIR", got["GI.comp__norestir_rt.spv"])


if __name__ == "__main__":
    unittest.main()
