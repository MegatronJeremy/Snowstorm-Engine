#!/usr/bin/env python3
"""Unit tests for the report formatting in vulkan-caps.py (pure: no GPU, no build)."""
import importlib.util
from pathlib import Path

_dir = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("vulkan_caps", _dir / "vulkan-caps.py")
vc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vc)


def shape(m, n, k, a="f16", b="f16", c="f32", r="f32", scope="subgroup"):
    return {"M": m, "N": n, "K": k, "A": a, "B": b, "C": c, "Result": r, "scope": scope, "saturatingAccumulation": False}


def test_f16_shape_requires_subgroup_scope_and_f16_inputs():
    assert vc.has_f16_shape({"cooperativeMatrix": {"shapes": [shape(16, 16, 16)]}})
    assert not vc.has_f16_shape({"cooperativeMatrix": {"shapes": [shape(16, 16, 16, scope="workgroup")]}})
    assert not vc.has_f16_shape({"cooperativeMatrix": {"shapes": [shape(16, 16, 16, a="s8", b="s8")]}})
    assert not vc.has_f16_shape({})


def test_shapes_sorted_and_stable():
    doc = {"cooperativeMatrix": {"shapes": [shape(16, 16, 16, a="s8", b="s8"), shape(16, 16, 16)]}}
    lines = vc.shapes_table(doc)
    assert lines[0].startswith("16x16x16  A=f16") and lines[1].startswith("16x16x16  A=s8")


def test_summary_of_unsupported_device_does_not_raise():
    text = "\n".join(vc.summarize({"device": "X", "cooperativeMatrix": {"supported": False, "shapes": []}}))
    assert "f16 x f16 subgroup shape: NO" in text
