#!/usr/bin/env python3
"""Unit tests for check-hlsl-layout.py (no GPU, no dxc): packing rules, a match, a mismatch, an unparseable struct.
Run directly (`py Scripts/test_check_hlsl_layout.py`, exit 0/1) or under pytest if installed.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("chl", Path(__file__).resolve().parent / "check-hlsl-layout.py")
chl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(chl)

HPP = "struct S {};\nstatic_assert(sizeof(S) == 32, \"x\");\n"
HLSL = "struct S\n{\n\tfloat3 A; // c\n\tfloat B;\n\tfloat3 C;\n\tint D;\n};\n"


def test_match():
    assert chl.check_one("S", HPP, HLSL, "S", "cbuffer") == (32, 32)


def test_mismatch_when_a_field_is_added_to_the_shader_only():
    cpp, hlsl = chl.check_one("S", HPP, HLSL.replace("int D;", "int D;\n\tfloat E;"), "S", "cbuffer")
    assert (cpp, hlsl) == (32, 48)


def test_unparseable_struct():
    for text, name in (("struct S { float3 A[; };", "S"), ("struct Other { float A; };", "S"), ("struct S { Foo A; };", "S")):
        try:
            chl.check_one("S", HPP, text, name, "cbuffer")
        except chl.LayoutError:
            continue
        raise AssertionError(f"expected LayoutError for {text!r}")


def test_missing_cpp_assert():
    try:
        chl.cpp_asserted_size("struct S {};", "S")
    except chl.LayoutError:
        return
    raise AssertionError("expected LayoutError")


def test_rules_differ_on_vec3():
    members = chl.parse_hlsl_struct("struct S { float A; float3 B; };", "S")
    assert chl.struct_size(members, "std430") == 32   # B aligned to 16
    assert chl.struct_size(members, "std430v3") == 16  # B at 4
    assert chl.struct_size(members, "cbuffer") == 16   # B fits the first row after A


def test_committed_table_matches():
    for struct, (hpp, hlsl, name, rule) in chl.TABLE.items():
        cpp, shader = chl.check_one(struct, (chl.ROOT / hpp).read_text(encoding="utf-8"),
                                    (chl.ROOT / hlsl).read_text(encoding="utf-8"), name, rule)
        assert cpp == shader, struct


if __name__ == "__main__":
    import sys
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok  ", name)
            except Exception as e:
                failed += 1
                print("FAIL", name, repr(e))
    sys.exit(1 if failed else 0)
