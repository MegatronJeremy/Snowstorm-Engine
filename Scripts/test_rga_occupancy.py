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
    # gfx11 wave32, allocation granularity 12. These expectations previously encoded the granularity of 16
    # the public ISA documentation implies; RGA's own per-compile report ("VGPR allocation granularity: 12",
    # and 167 requested allocating 168 rather than 176) says 12, which is what rga-occupancy.py models.
    # Only 97, 128 and 256 below can tell the two apart: 96 and 120 give the same wave count either way.
    assert rga.vgpr_occupancy(1, "gfx1100") == 16
    assert rga.vgpr_occupancy(96, "gfx1100") == 16      # threshold: 1536/96 = 16
    assert rga.vgpr_occupancy(97, "gfx1100") == 14      # round up to 108 => 1536/108 = 14.2 -> 14
    assert rga.vgpr_occupancy(120, "gfx1100") == 12     # 120 is already a multiple of 12 => 1536/120 -> 12
    assert rga.vgpr_occupancy(128, "gfx1100") == 11     # round up to 132 => 1536/132 = 11.6 -> 11
    assert rga.vgpr_occupancy(192, "gfx1100") == 8      # GIDenoise: the one occupancy-limited shader
    assert rga.vgpr_occupancy(256, "gfx1100") == 5      # round up to 264 => 1536/264 = 5.8 -> 5


def test_vgpr_occupancy_gfx12_differs_from_gfx11():
    # RDNA4 shares the register file and wave slots but allocates in blocks of 24, so the wave count steps
    # at different VGPR counts. Pinned because the two models are interchangeable at a glance and are not:
    # collapsing them would silently mis-report occupancy on whichever family lost.
    assert rga.vgpr_occupancy(96, "gfx1200") == 16      # multiple of 24 => 1536/96 = 16
    assert rga.vgpr_occupancy(97, "gfx1200") == 12      # round up to 120 => 1536/120 = 12.8 -> 12
    assert rga.vgpr_occupancy(192, "gfx1200") == 8      # multiple of 24 => 1536/192 = 8
    disagreements = sum(1 for v in range(1, 257)
                        if rga.vgpr_occupancy(v, "gfx1100") != rga.vgpr_occupancy(v, "gfx1200"))
    assert disagreements > 0, "gfx11 and gfx12 occupancy models collapsed to the same curve"


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
