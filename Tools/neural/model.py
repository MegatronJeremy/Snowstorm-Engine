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

# Hidden width + output channels are fixed; the input width selects spatial (3) vs temporal (8).
HIDDEN = 16
OUT_CH = 3
KERNEL = 3


class ResidualRefiner(nn.Module):
    """Post-upsample residual CNN. forward(features) = features[:, 0:3] + conv_stack(features).

    `features` is (N, in_ch, H, W); channels 0..2 are the bilinear base (the residual skip). in_ch=3 is
    the spatial path (#47); in_ch=8 is the temporal path (#98: +warped history +motion vector)."""

    def __init__(self, in_channels: int = 3) -> None:
        super().__init__()
        self.in_channels = in_channels
        pad = KERNEL // 2
        self.conv0 = nn.Conv2d(in_channels, HIDDEN, KERNEL, padding=pad)
        self.conv1 = nn.Conv2d(HIDDEN, HIDDEN, KERNEL, padding=pad)
        self.conv2 = nn.Conv2d(HIDDEN, OUT_CH, KERNEL, padding=pad)
        self.relu = nn.ReLU(inplace=True)

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        base = features[:, 0:OUT_CH]  # bilinear RGB — the residual skip (matches NeuralResidualAdd)
        x = self.relu(self.conv0(features))
        x = self.relu(self.conv1(x))
        residual = self.conv2(x)  # linear output layer
        return base + residual

    def to_layers(self) -> list[Layer]:
        """Export the three conv layers to the .ssnn Layer list (weights already in [outC][inC][kH][kW])."""
        specs = [(self.conv0, ACT_RELU), (self.conv1, ACT_RELU), (self.conv2, ACT_NONE)]
        layers: list[Layer] = []
        for conv, act in specs:
            w = conv.weight.detach().cpu().contiguous().view(-1).tolist()  # [outC,inC,kH,kW] row-major
            b = conv.bias.detach().cpu().contiguous().view(-1).tolist()
            layers.append(Layer(conv.in_channels, conv.out_channels, KERNEL, act, w, b))
        return layers

    @classmethod
    def from_layers(cls, layers: list[Layer]) -> "ResidualRefiner":
        """Rebuild the model from a loaded .ssnn Layer list (inverse of to_layers). The .ssnn stores each
        conv's weights row-major in [outC][inC][kH][kW] — exactly PyTorch's conv-weight layout — so the
        flat arrays reshape back 1:1. The first layer's in_channels selects spatial (3) vs temporal (8)."""
        assert len(layers) == 3, f"expected 3 layers, got {len(layers)}"
        m = cls(in_channels=layers[0].in_channels)
        convs = [m.conv0, m.conv1, m.conv2]
        with torch.no_grad():
            for conv, layer in zip(convs, layers):
                w = torch.tensor(layer.weights, dtype=torch.float32).view(
                    layer.out_channels, layer.in_channels, layer.kernel_size, layer.kernel_size)
                conv.weight.copy_(w)
                conv.bias.copy_(torch.tensor(layer.bias, dtype=torch.float32))
        return m


def make_identity(in_channels: int = 3) -> ResidualRefiner:
    """The identity refiner: final conv (weights + bias) is all-zero, so the residual is 0 and the
    output == the bilinear base (features 0..2). Matches MakeIdentityRefiner(inChannels) so the exported
    .ssnn is byte-identical to the engine's dump — the parity oracle."""
    m = ResidualRefiner(in_channels=in_channels)
    with torch.no_grad():
        for conv in (m.conv0, m.conv1, m.conv2):
            conv.weight.zero_()
            conv.bias.zero_()
    return m
