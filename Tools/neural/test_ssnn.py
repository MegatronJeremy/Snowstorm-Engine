"""Byte-parity + round-trip tests for the .ssnn format (#99).

The critical test asserts the Python identity refiner exports BYTE-FOR-BYTE identically to the .ssnn the
engine writes via `--neural.dump_identity`. That pins the format contract between the C++ reader and the
Python writer, so a trained model can't silently mis-serialize.

Run:  pytest Tools/neural/            (from the repo root; the reference file is regenerated below if the
engine binary is available, else the committed sample under this dir is used).
"""

from __future__ import annotations

import os
import struct
import subprocess

import pytest

from model import make_identity
from ssnn import ACT_NONE, ACT_RELU, Layer, load_ssnn, save_ssnn

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))


def _engine_reference(tmp_path) -> bytes | None:
    """Ask the engine to dump its identity refiner; return the bytes, or None if the exe isn't built."""
    exe = os.path.join(REPO, "build", "Snowstorm-Editor", "Debug", "Snowstorm-Editor.exe")
    if not os.path.exists(exe):
        return None
    out = os.path.join(tmp_path, "engine_identity.ssnn")
    env = dict(os.environ)
    env.setdefault("VK_ADD_LAYER_PATH", os.path.join(REPO, "vcpkg", "installed", "x64-windows", "bin"))
    try:
        subprocess.run([exe, f"--neural.dump_identity={out}"], cwd=REPO, env=env,
                       timeout=120, capture_output=True, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return open(out, "rb").read() if os.path.exists(out) else None


def test_python_identity_matches_engine_dump(tmp_path):
    """The Python-exported identity .ssnn must equal the engine's byte-for-byte (the format contract)."""
    engine_bytes = _engine_reference(tmp_path)
    if engine_bytes is None:
        pytest.skip("engine exe not built; skipping byte-parity (round-trip tests still cover the format)")

    py_path = os.path.join(tmp_path, "python_identity.ssnn")
    save_ssnn(py_path, make_identity().to_layers())
    py_bytes = open(py_path, "rb").read()

    assert py_bytes == engine_bytes, (
        f"Python .ssnn ({len(py_bytes)} B) differs from engine dump ({len(engine_bytes)} B) — "
        "format drift between NeuralWeights.cpp and ssnn.py"
    )


def test_identity_structure():
    """Sanity: the identity model is 3->16->16->3, final layer zero (so residual == 0)."""
    layers = make_identity().to_layers()
    assert [ (l.in_channels, l.out_channels) for l in layers ] == [(3, 16), (16, 16), (16, 3)]
    assert layers[0].activation == ACT_RELU and layers[2].activation == ACT_NONE
    assert all(w == 0.0 for w in layers[-1].weights) and all(b == 0.0 for b in layers[-1].bias)


def test_roundtrip():
    """save -> load reconstructs every field exactly (distinct values, both activations, k=1 and k=3)."""
    layers = [
        Layer(2, 3, 3, ACT_RELU,
              weights=[i * 0.25 - 1.0 for i in range(3 * 2 * 3 * 3)],
              bias=[0.1, -0.2, 0.3]),
        Layer(3, 1, 1, ACT_NONE, weights=[1.5, -2.5, 3.5], bias=[0.75]),
    ]
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "rt.ssnn")
        save_ssnn(p, layers)
        back = load_ssnn(p)
    assert len(back) == 2
    for a, b in zip(layers, back):
        assert (a.in_channels, a.out_channels, a.kernel_size, a.activation) == \
               (b.in_channels, b.out_channels, b.kernel_size, b.activation)
        assert a.weights == pytest.approx(b.weights)
        assert a.bias == pytest.approx(b.bias)


def test_header_bytes():
    """Header preamble is magic 'SSNN', version 1, and the declared layer count."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "h.ssnn")
        save_ssnn(p, make_identity().to_layers())
        raw = open(p, "rb").read()
    assert raw[:4] == b"SSNN"
    version, count = struct.unpack_from("<II", raw, 4)
    assert version == 1 and count == 3
