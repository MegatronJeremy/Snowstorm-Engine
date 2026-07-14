"""The neural super-resolution refiner, mirroring the engine's NeuralUpscalePass architecture (#99/#98).

The engine computes: output = bilinear(LR) + conv_stack(features). The conv stack is
in_ch -> 16 -> 16 -> 3, all 3x3 "same" padding, ReLU on the two hidden layers, linear output. So the
network learns a RESIDUAL added onto the bilinear upsample (feature channels 0..2). The Python model
must be structured the same way, and its weights export to .ssnn in the engine's [outC][inC][kH][kW]
layout (exactly PyTorch's conv weight order), so a trained state_dict maps 1:1.

Two input widths, matching the two engine inference paths:
  - SPATIAL (#47, in_ch=3): features = bilinear(LR) RGB only.
  - TEMPORAL (#98, in_ch=8): features = [bilinear(LR) RGB (0..2), MV-warped previous output RGB (3..5),
    motion vector xy (6..7)]. The residual is still added to channels 0..2 (the bilinear base), so the
    net refines the bilinear image using the warped history + motion as side information — the
    DLSS/XeSS pattern. The FIRST 3 channels of the feature tensor are ALWAYS the bilinear base.
"""

from __future__ import annotations

import torch
import torch.nn as nn

from ssnn import ACT_NONE, ACT_RELU, Layer

# Output channels + kernel are fixed. Hidden width + depth are CONFIGURABLE (the .ssnn format and the
# engine's NeuralConv loop already run any layer list, so a bigger net is a pure training-side change —
# only MakeIdentityRefiner's fixed 3-layer shape is a C++ constant, and identity weights aren't trained).
OUT_CH = 3
KERNEL = 3
DEFAULT_HIDDEN = 32
DEFAULT_DEPTH = 4  # total conv layers: (depth-1) hidden ReLU layers + 1 linear output layer


class ResidualRefiner(nn.Module):
    """Post-upsample residual CNN. forward(features) = features[:, 0:3] + conv_stack(features).

    `features` is (N, in_ch, H, W); channels 0..2 are the bilinear base (the residual skip). in_ch=3 is
    the spatial path (#47); in_ch=8 is the temporal path (#98: +warped history +motion vector). `hidden`
    and `depth` scale capacity: depth = total conv layers (depth-1 ReLU hidden + 1 linear output)."""

    def __init__(self, in_channels: int = 3, hidden: int = DEFAULT_HIDDEN, depth: int = DEFAULT_DEPTH) -> None:
        super().__init__()
        assert depth >= 2, "need at least one hidden + one output layer"
        self.in_channels = in_channels
        self.hidden = hidden
        self.depth = depth
        pad = KERNEL // 2
        chans = [in_channels] + [hidden] * (depth - 1) + [OUT_CH]
        self.convs = nn.ModuleList(nn.Conv2d(chans[i], chans[i + 1], KERNEL, padding=pad) for i in range(depth))
        self.relu = nn.ReLU(inplace=True)

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        base = features[:, 0:OUT_CH]  # bilinear RGB — the residual skip (matches NeuralResidualAdd)
        x = features
        for i, conv in enumerate(self.convs):
            x = conv(x)
            if i < len(self.convs) - 1:  # ReLU on every layer except the linear output
                x = self.relu(x)
        return base + x  # x is the residual

    def to_layers(self) -> list[Layer]:
        """Export every conv layer to the .ssnn Layer list (weights already in [outC][inC][kH][kW]). All but
        the last are ReLU; the output layer is linear."""
        layers: list[Layer] = []
        for i, conv in enumerate(self.convs):
            act = ACT_NONE if i == len(self.convs) - 1 else ACT_RELU
            w = conv.weight.detach().cpu().contiguous().view(-1).tolist()  # [outC,inC,kH,kW] row-major
            b = conv.bias.detach().cpu().contiguous().view(-1).tolist()
            layers.append(Layer(conv.in_channels, conv.out_channels, KERNEL, act, w, b))
        return layers

    @classmethod
    def from_layers(cls, layers: list[Layer]) -> "ResidualRefiner":
        """Rebuild the model from a loaded .ssnn Layer list (inverse of to_layers). The .ssnn stores each
        conv's weights row-major in [outC][inC][kH][kW] — exactly PyTorch's conv-weight layout — so the
        flat arrays reshape back 1:1. Infers in_channels/hidden/depth from the layer list, so a wider/deeper
        trained model loads without the caller knowing its shape."""
        assert len(layers) >= 2, f"expected >= 2 layers, got {len(layers)}"
        m = cls(in_channels=layers[0].in_channels, hidden=layers[0].out_channels, depth=len(layers))
        with torch.no_grad():
            for conv, layer in zip(m.convs, layers):
                w = torch.tensor(layer.weights, dtype=torch.float32).view(
                    layer.out_channels, layer.in_channels, layer.kernel_size, layer.kernel_size)
                conv.weight.copy_(w)
                conv.bias.copy_(torch.tensor(layer.bias, dtype=torch.float32))
        return m


def make_identity(in_channels: int = 3) -> ResidualRefiner:
    """The identity refiner: ALL conv weights + biases zero, so the residual is 0 and the output == the
    bilinear base (features 0..2). Fixed to the engine's 3-layer, 16-hidden shape (MakeIdentityRefiner in
    NeuralWeights.cpp) so the exported .ssnn is byte-identical to the engine's dump — the parity oracle.
    Trained models use the larger DEFAULT_HIDDEN/DEFAULT_DEPTH; identity stays small since it's not trained."""
    m = ResidualRefiner(in_channels=in_channels, hidden=16, depth=3)
    with torch.no_grad():
        for conv in m.convs:
            conv.weight.zero_()
            conv.bias.zero_()
    return m
