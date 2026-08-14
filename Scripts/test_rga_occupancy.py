#!/usr/bin/env python3
"""Unit tests for the pure logic in rga-occupancy.py (no RGA / no GPU needed).

Covers the occupancy model (verified against AMD's documented gfx11 examples), the CSV parsing
(esp. that USED_* columns win over AVAILABLE_*), base-name derivation, and permutation collapse.
Run directly (`py Scripts/test_rga_occupancy.py`, exit 0/1) or under pytest if installed.
"""
import importlib.util
import tempfile
from pathlib import Path

_spec = importlib.util.spec_from_file_location("rga_occ", Path(__file__).resolve().parent / "rga-occupancy.py")
rga = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rga)


def test_vgpr_occupancy_matches_amd_model():
    # AMD's documented gfx11 wave32 examples: <=96 => 16 waves; 120 => 12; and the wave boundaries.
    assert rga.vgpr_occupancy(1, "gfx1100") == 16
    assert rga.vgpr_occupancy(96, "gfx1100") == 16      # threshold: 1536/96 = 16
    assert rga.vgpr_occupancy(97, "gfx1100") == 13      # round up to 112 => 1536/112 = 13.7 -> 13
    assert rga.vgpr_occupancy(120, "gfx1100") == 12     # AMD example: 128-alloc => 1536/128 = 12
    assert rga.vgpr_occupancy(128, "gfx1100") == 12
    assert rga.vgpr_occupancy(192, "gfx1100") == 8      # GIDenoise: the one occupancy-limited shader
    assert rga.vgpr_occupancy(256, "gfx1100") == 6      # 1536/256 = 6


def test_vgpr_occupancy_guards():
    assert rga.vgpr_occupancy(0, "gfx1100") is None     # no data
    assert rga.vgpr_occupancy(71, "gfx1030") is None    # non-gfx11: model doesn't apply


def test_base_shader_name():
    assert rga.base_shader_name(Path("DefaultLit.frag_e24aa6535195213a.spv")) == "DefaultLit.frag"
    assert rga.base_shader_name(Path("IBLBRDFLut_d0686fcf4f709da5.spv")) == "IBLBRDFLut"
    assert rga.base_shader_name(Path("Reflection.comp_rt.spv")) == "Reflection.comp"


def test_parse_rga_csv_prefers_used_over_available():
    # Real RGA 2.14.2 header. The alias order must pick USED_LDS_BYTES (4096), not AVAILABLE (65536),
    # and USED_VGPRs (71), not AVAILABLE_VGPRs (256) -- the bug the fuzzy matcher had to avoid.
    header = ("DEVICE,SCRATCH_MEM,THREADS_PER_WORKGROUP,WAVEFRONT_SIZE,AVAILABLE_LDS_BYTES,"
              "USED_LDS_BYTES,AVAILABLE_SGPRs,USED_SGPRs,SGPR_SPILLS,AVAILABLE_VGPRs,USED_VGPRs,"
              "VGPR_SPILLS,CL_WORKGROUP_X_DIMENSION,CL_WORKGROUP_Y_DIMENSION,CL_WORKGROUP_Z_DIMENSION,ISA_SIZE")
    row = "gfx1100,0,0,0,65536,4096,106,45,0,256,71,0,0,0,0,4940"
    tmp = Path(tempfile.mkdtemp()) / "gfx1100_stats_comp.csv"
    tmp.write_text(header + "\n" + row + "\n")
    m = rga.parse_rga_csv(tmp)
    assert m["vgprs"] == 71 and m["sgprs"] == 45
    assert m["lds"] == 4096          # USED, not the 65536 available
    assert m["scratch"] == 0 and m["vgpr_spills"] == 0 and m["sgpr_spills"] == 0
    assert m["isa_size"] == 4940


def test_collapse_worst_takes_max():
    worst = rga.collapse_worst([
        {"vgprs": 64, "scratch": 0, "isa_size": 100},
        {"vgprs": 96, "scratch": 8, "isa_size": 90},
    ])
    assert worst["vgprs"] == 96 and worst["scratch"] == 8 and worst["isa_size"] == 100
    assert worst["permutations"] == 2


def _run_all() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failed = 0
    for t in tests:
        try:
            t()
            print(f"  ok   {t.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"  FAIL {t.__name__}: {e}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    import sys
    sys.exit(_run_all())
