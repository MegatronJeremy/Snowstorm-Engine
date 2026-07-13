"""The neural super-resolution refiner, mirroring the engine's NeuralUpscalePass architecture (#99).

The engine computes: output = bilinear(LR) + conv_stack(bilinear(LR)). The conv stack is
3 -> 16 -> 16 -> 3, all 3x3 "same" padding, ReLU on the two hidden layers, linear output. So the
network learns a RESIDUAL on top of the bilinear upsample. The Python model must be structured the same
way, and its weights must export to .ssnn in the engine's [outC][inC][kH][kW] layout (which is exactly
PyTorch's conv weight order), so a trained state_dict maps 1:1.
"""

from __future__ import annotations

import torch
import torch.nn as nn

from ssnn import ACT_NONE, ACT_RELU, Layer

# Architecture constants — keep in lock-step with MakeIdentityRefiner in NeuralWeights.cpp.
CHANNELS = [3, 16, 16, 3]
KERNEL = 3


class ResidualRefiner(nn.Module):
    """Post-upsample residual CNN. forward(bilinear) = bilinear + conv_stack(bilinear)."""

    def __init__(self) -> None:
        super().__init__()
        pad = KERNEL // 2
        self.conv0 = nn.Conv2d(CHANNELS[0], CHANNELS[1], KERNEL, padding=pad)
        self.conv1 = nn.Conv2d(CHANNELS[1], CHANNELS[2], KERNEL, padding=pad)
        self.conv2 = nn.Conv2d(CHANNELS[2], CHANNELS[3], KERNEL, padding=pad)
        self.relu = nn.ReLU(inplace=True)

    def forward(self, bilinear: torch.Tensor) -> torch.Tensor:
        x = self.relu(self.conv0(bilinear))
        x = self.relu(self.conv1(x))
        residual = self.conv2(x)  # linear output layer
        return bilinear + residual

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
        flat arrays reshape back 1:1. Used by the offline full-frame eval to run the trained net in Python."""
        m = cls()
        convs = [m.conv0, m.conv1, m.conv2]
        assert len(layers) == len(convs), f"expected {len(convs)} layers, got {len(layers)}"
        with torch.no_grad():
            for conv, layer in zip(convs, layers):
                w = torch.tensor(layer.weights, dtype=torch.float32).view(
                    layer.out_channels, layer.in_channels, layer.kernel_size, layer.kernel_size)
                conv.weight.copy_(w)
                conv.bias.copy_(torch.tensor(layer.bias, dtype=torch.float32))
        return m


def make_identity() -> ResidualRefiner:
    """The identity refiner: final conv (weights + bias) is all-zero, so the residual is 0 and the
    output == the bilinear input. Matches MakeIdentityRefiner (all layers zeroed) so the exported
    .ssnn is byte-identical to the engine's dump — the parity oracle."""
    m = ResidualRefiner()
    with torch.no_grad():
        for conv in (m.conv0, m.conv1, m.conv2):
            conv.weight.zero_()
            conv.bias.zero_()
    return m
