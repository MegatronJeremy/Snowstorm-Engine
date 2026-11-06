#!/usr/bin/env python3
"""Unit tests for the pure logic in shader-feedback.py (no RGA, no GPU)."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("sfb", Path(__file__).resolve().parent / "shader-feedback.py")
sfb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sfb)


def test_count_isa_instructions_skips_labels_and_comments():
    isa = "\n".join(["; header", "_amdgpu_cs_main:", "\ts_mov_b32 s0, 0", "\tv_add_f32 v0, v1, v2",
                     "label_0:", "\tglobal_load_b32 v1, v2, s[0:1]", "\t// note", "\ts_endpgm", ""])
    assert sfb.count_isa_instructions(isa) == 4


def _perm(v, instr=100, lds=0):
    return {"vgprs": v, "sgprs": 20, "lds": lds, "scratch": 0, "vgpr_spills": 0, "sgpr_spills": 0,
            "isa_size": 800, "instructions": instr}


def test_shader_entry_is_worst_permutation_with_occupancy():
    e = sfb.shader_entry("comp", [_perm(96, 90), _perm(128, 120)], "gfx1200")
    assert (e["vgprs"], e["instructions"], e["permutations"]) == (128, 120, 2)
    assert e["waves"] == 10 and e["max_waves"] == 16     # 128 -> 144 allocated on gfx12, 1536/144 = 10


def test_missing_isa_listing_leaves_instructions_null():
    e = sfb.shader_entry("comp", [_perm(64), {**_perm(64), "instructions": None}], "gfx1200")
    assert e["instructions"] is None


def _report(asic, **shaders):
    return sfb.build_report({asic: {n: sfb.shader_entry("comp", [p], asic) for n, p in shaders.items()}}, {})


def test_diff_lists_only_changes_and_new_removed():
    base = _report("gfx1200", A=_perm(96), B=_perm(64), C=_perm(32))
    head = _report("gfx1200", A=_perm(128, 130), B=_perm(64), D=_perm(40))
    d = sfb.diff_reports(base, head)["gfx1200"]
    assert list(d["changed"]) == ["A"] and d["added"] == ["D"] and d["removed"] == ["C"]
    assert d["changed"]["A"]["vgprs"] == (96, 128)
    assert d["changed"]["A"]["waves"] == (16, 10)
    md = sfb.render_markdown(sfb.diff_reports(base, head), head, base)
    assert md.startswith(sfb.MARKER) and "96 → 128 (+32) ⚠" in md and "16 → 10 (-6) ⚠" in md
    assert "not a merge gate" in md


def test_unchanged_reports_say_so_and_errors_are_listed():
    r = _report("gfx1200", A=_perm(96))
    r["errors"] = {"gfx1151": "unsupported"}
    md = sfb.render_markdown(sfb.diff_reports(r, r), r, r)
    assert "No change" in md and "gfx1151: unsupported" in md


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("ok")
