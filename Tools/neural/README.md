# Neural upscaler training harness (#99)

Offline PyTorch pipeline that trains the neural super-resolution refiner on data exported by the engine
(`dataset.export`, #46) and writes a `.ssnn` weights file the engine loads (`neural.weights`, #47/#99).

## Layout

- `ssnn.py` — read/write the `.ssnn` weights format. **Byte-matches** the C++ reader/writer in
  `Snowstorm-Core/Source/Snowstorm/Render/Neural/NeuralWeights.cpp`.
- `model.py` — `ResidualRefiner`, the same architecture as the engine's `NeuralUpscalePass`
  (`output = bilinear + convStack(bilinear)`, 3→16→16→3 3×3, ReLU hidden, linear out).
- `test_ssnn.py` — round-trip + **byte-parity** tests (the Python identity `.ssnn` must equal the
  engine's `--neural.dump_identity` output).
- `train.py`, `dataset.py` — dataset loader + trainer (added in a later increment).

## Setup

```
pip install -r Tools/neural/requirements.txt   # torch (CPU is fine), numpy, pytest
```

## Verify the format contract

From the repo root (with the editor built in Debug so the byte-parity test can dump a reference):

```
pytest Tools/neural/
```

The `test_python_identity_matches_engine_dump` test spawns `Snowstorm-Editor.exe --neural.dump_identity`
and asserts the Python-written identity `.ssnn` is byte-identical. If the editor isn't built, that one
test skips and the round-trip tests still cover the format.

## The .ssnn format

Little-endian, tightly packed (see `ssnn.py` / `NeuralWeights.cpp`):

```
char[4]  magic   = "SSNN"
uint32   version = 1
uint32   layer_count
per layer: uint32 in, out, kernel, activation(0 none / 1 relu),
           float32[out*in*kh*kw] weights ([outC][inC][kH][kW]),  float32[out] bias
```
