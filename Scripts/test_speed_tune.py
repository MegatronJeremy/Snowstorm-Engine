#!/usr/bin/env python3
"""Tests for speed-tune.py's pure parts (no GPU): constraint, Deb's rules, DE, decoding. Run directly or under pytest."""
import importlib.util
import random
from pathlib import Path

_spec = importlib.util.spec_from_file_location("speed_tune", Path(__file__).resolve().parent / "speed-tune.py")
st = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(st)

BASE = {"jod": 5.0, "tflip": 0.010}


def test_violation_zero_inside_margins():
    assert st.violation({"jod": 4.99, "tflip": 0.0105}, BASE, 0.02, 0.001) == 0


def test_violation_counts_both_terms():
    v = st.violation({"jod": 4.90, "tflip": 0.0130}, BASE, 0.02, 0.001)
    assert abs(v - (0.08 + 0.002)) < 1e-9


def test_missing_metric_is_infeasible():
    assert st.violation({"jod": None, "tflip": 0.01}, BASE, 0.02, 0.001) == float("inf")


def test_deb_rules():
    f = lambda ms, v: {"ms": ms, "violation": v}
    assert st.better(f(9, 0), f(5, 0.1))
    assert not st.better(f(5, 0.1), f(9, 0))
    assert st.better(f(5, 0.05), f(4, 0.1))
    assert st.better(f(4, 0), f(5, 0))
    assert not st.better(f(5, 0), f(5, 0))


def test_decode_rounds_ints_and_clamps():
    params = [("A", 1, 4, True, 2), ("B", 0.25, 1.0, False, 0.5)]
    assert st.decode([0.5, 2.0], params) == {"A": "2", "B": "1.0000"}
    assert st.decode(st.default_vector(params), params) == {"A": "2", "B": "0.5000"}


def test_de_finds_cheapest_feasible_and_never_worse_than_seed():
    # time falls with x; quality (a proxy) is lost below x=0.4, so the optimum sits at the constraint edge
    def evaluate(v):
        x = v[0]
        return {"ms": 10 + 10 * x, "violation": max(0.0, 0.4 - x)}
    seed = [0.9]
    (vec, fit), evals = st.differential_evolution(evaluate, 1, seed, 8, 25, random.Random(3), log=lambda *_: None)
    assert fit["violation"] == 0
    assert fit["ms"] <= 10 + 10 * 0.9
    assert 0.39 <= vec[0] <= 0.5
    assert len(evals) == 8 + 8 * 25


def test_settings_diff_lists_only_moved_knobs():
    params = [("A", 1, 4, True, 2), ("B", 0.25, 1.0, False, 0.5)]
    assert st.settings_diff({"A": "2", "B": "0.7500"}, params) == [("B", "0.5000", "0.7500")]


if __name__ == "__main__":
    for n, fn in list(globals().items()):
        if n.startswith("test_"):
            fn()
            print("ok", n)
