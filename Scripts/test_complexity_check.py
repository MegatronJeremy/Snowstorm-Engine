#!/usr/bin/env python3
"""Unit tests for the pure parts of complexity-check.py (no clang-tidy, no compile database).
Run directly (`py Scripts/test_complexity_check.py`, exit 0/1) or under pytest."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("complexity_check", Path(__file__).resolve().parent / "complexity-check.py")
cc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cc)

LIMIT = 25


def test_new_function_over_the_limit_fails():
    failures, notes = cc.evaluate({"a.cpp::F": 26}, {}, LIMIT)
    assert len(failures) == 1 and "a.cpp::F" in failures[0] and not notes


def test_at_the_limit_passes():
    assert cc.evaluate({"a.cpp::F": 25}, {}, LIMIT) == ([], [])


def test_baselined_function_may_stay_but_not_get_worse():
    assert cc.evaluate({"a.cpp::F": 40}, {"a.cpp::F": 40}, LIMIT) == ([], [])
    assert cc.evaluate({"a.cpp::F": 35}, {"a.cpp::F": 40}, LIMIT) == ([], [])
    failures, _ = cc.evaluate({"a.cpp::F": 41}, {"a.cpp::F": 40}, LIMIT)
    assert len(failures) == 1 and "40 in the baseline" in failures[0]


def test_baseline_entry_back_under_or_gone_is_reported():
    _, notes = cc.evaluate({"a.cpp::F": 20}, {"a.cpp::F": 40, "b.cpp::G": 30}, LIMIT)
    assert any("a.cpp::F is at 20" in n for n in notes)
    assert any("b.cpp::G is gone" in n for n in notes)


def test_over_limit_keeps_only_functions_above_it():
    assert cc.over_limit({"a::F": 26, "b::G": 25, "c::H": 3}, LIMIT) == {"a::F": 26}


def test_scored_paths():
    repo = cc.REPO
    assert cc.is_scored(str(repo / "Snowstorm-Core/Source/Snowstorm/Systems/RenderSystem.cpp"))
    assert cc.is_scored(str(repo / "Games/Pong/Source/PongSystem.cpp"))
    assert not cc.is_scored(str(repo / "Snowstorm-Tests/Source/HashTests.cpp"))
    assert not cc.is_scored(str(repo / "Snowstorm-Core/Source/Platform/Vulkan/imgui_impl_vulkan.cpp"))
    assert not cc.is_scored(str(repo.parent / "elsewhere.cpp"))


def test_pch_flags_are_stripped_and_force_include_kept():
    entries = [{"file": "x.cpp", "command": "cl.exe /nologo /Yuc:/b/cmake_pch.hxx /Fpc:/b/p.pch /FIc:/b/cmake_pch.hxx /c x.cpp"}]
    command = cc.without_pch(entries)[0]["command"]
    assert "/Yu" not in command and "/Fp" not in command and "/FIc:/b/cmake_pch.hxx" in command


if __name__ == "__main__":
    import sys
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for t in tests:
        try:
            t()
        except AssertionError as e:
            failed += 1
            print(f"FAIL {t.__name__}: {e}")
    print(f"{len(tests) - failed}/{len(tests)} passed")
    sys.exit(1 if failed else 0)
