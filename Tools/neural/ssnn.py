"""Read/write the Snowstorm .ssnn neural-weights format (#99).

This MUST byte-match the C++ reader/writer in
Snowstorm-Core/Source/Snowstorm/Render/Neural/NeuralWeights.cpp (SaveModel/LoadModel), so a model
trained here loads verbatim in the engine. The byte-parity is pinned by test_ssnn.py against a file the
engine itself dumps (--neural.dump_identity).

Format (little-endian, tightly packed):
    char[4]  magic   = b"SSNN"
    uint32   version = 1
    uint32   layer_count
    per layer:
        uint32  in_channels
        uint32  out_channels
        uint32  kernel_size
        uint32  activation      # 0 = none, 1 = ReLU
        float32[out*in*kh*kw]   weights, laid out [outC][inC][kH][kW]  (PyTorch conv weight order)
        float32[out]            bias
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"SSNN"
VERSION = 1

ACT_NONE = 0
ACT_RELU = 1


@dataclass
class Layer:
    in_channels: int
    out_channels: int
    kernel_size: int
    activation: int          # ACT_NONE / ACT_RELU
    weights: list[float]     # length out*in*k*k, order [outC][inC][kH][kW]
    bias: list[float]        # length out

    def weight_count(self) -> int:
        return self.out_channels * self.in_channels * self.kernel_size * self.kernel_size


def save_ssnn(path: str, layers: list[Layer]) -> None:
    """Serialize `layers` to `path` in the exact .ssnn byte layout the engine reads."""
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", VERSION, len(layers)))
        for ly in layers:
            assert len(ly.weights) == ly.weight_count(), "weight count mismatch"
            assert len(ly.bias) == ly.out_channels, "bias count mismatch"
            f.write(struct.pack("<IIII", ly.in_channels, ly.out_channels, ly.kernel_size, ly.activation))
            f.write(struct.pack(f"<{len(ly.weights)}f", *ly.weights))
            f.write(struct.pack(f"<{len(ly.bias)}f", *ly.bias))


def load_ssnn(path: str) -> list[Layer]:
    """Parse a .ssnn file (engine- or Python-written) back into a list of Layers."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != MAGIC:
        raise ValueError(f"{path}: bad magic {data[:4]!r}")
    off = 4
    version, layer_count = struct.unpack_from("<II", data, off)
    off += 8
    if version != VERSION:
        raise ValueError(f"{path}: unsupported version {version}")

    layers: list[Layer] = []
    for _ in range(layer_count):
        in_c, out_c, k, act = struct.unpack_from("<IIII", data, off)
        off += 16
        wn = out_c * in_c * k * k
        weights = list(struct.unpack_from(f"<{wn}f", data, off))
        off += wn * 4
        bias = list(struct.unpack_from(f"<{out_c}f", data, off))
        off += out_c * 4
        layers.append(Layer(in_c, out_c, k, act, weights, bias))
    return layers
