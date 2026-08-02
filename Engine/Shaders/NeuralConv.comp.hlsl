// Generic 2D convolution layer for the neural upscaler (#47). One dispatch = one conv layer: reads an input
// feature map, applies a "same"-padded stride-1 KxK convolution + bias + optional ReLU, writes the output
// feature map. The network architecture is the SEQUENCE of these dispatches (chained by C++ in
// NeuralUpscalePass) with per-layer weight tensors — the ONNX/TensorRT model, not one giant unrolled shader.
//
// Feature maps are flat CHW float buffers (channel c, row y, col x at c*H*W + y*W + x) — exactly the layout
// of the NeuralMath.hpp CPU reference, so the shader is validated 1:1 against it. This matches how ML
// runtimes hold tensors (flat NCHW) and needs only memory barriers between layers (no image-layout dance).
//
// Weights are [outC][inC][kH][kW] row-major (PyTorch), bias [outC], both in one buffer with the bias block
// appended after the weights (BiasOffset).
//
// PERF (#): the naive version re-read the input from global memory for every output channel AND every kernel
// tap — each input texel fetched ~(OutC * K*K) times, the dominant cost on this memory-bound conv. This
// version caches ONE input channel's halo tile in groupshared at a time (tiny LDS -> full occupancy, unlike a
// whole-tile-all-channels cache which collapses occupancy and measured SLOWER than naive), loops input
// channels accumulating into per-thread OUTPUT-channel registers, so each input texel hits global memory once
// per group per channel. The inner (oc, k) MAC over the LDS window is the register-GEMM row that a
// cooperative-matrix (tensor-core) rewrite later swaps in (#137). fp32 for now; fp16 is the next increment.

StructuredBuffer<float> InMap : register(t0, space0);   // CHW, InChannels*H*W floats
StructuredBuffer<float> Weights : register(t1, space0); // [outC*inC*k*k] then [outC] bias at BiasOffset
RWStructuredBuffer<float> OutMap : register(u2, space0); // CHW, OutChannels*H*W floats

cbuffer ConvCB : register(b3, space0)
{
	uint2 Size;        // feature-map W,H (input == output, same padding)
	uint InChannels;
	uint OutChannels;
	uint KernelSize;   // 1 or 3
	uint Activation;   // 0 none, 1 ReLU
	uint WeightOffset; // global float index where this layer's weights begin
	uint BiasOffset;   // global float index where this layer's bias begins
};

// 8x8 output tile per group, 1-texel halo (max KernelSize is 3 -> radius 1) -> 10x10 cached tile. ONE input
// channel cached at a time: 100 floats = 400 B LDS, negligible -> full occupancy (a whole-tile-all-channels
// cache was ~25 KB and collapsed occupancy, measured SLOWER than naive). MAX_OUT_CHANNELS caps the per-thread
// output accumulator register array; 64 covers the trained models (the "big" temporal one is 48-wide) with
// headroom. MUST match kMaxTileChannels in NeuralUpscalePass::EnsureModel (which falls back to identity past it).
#define TILE 8
#define HALO 1
#define TILE_DIM (TILE + 2 * HALO) // 10
#define MAX_OUT_CHANNELS 64

groupshared float gTile[TILE_DIM * TILE_DIM]; // current input channel's halo tile

// Global input at (x,y) channel c; zero outside the image (same-padding border).
float ReadInClamped(int x, int y, uint c)
{
	if (x < 0 || y < 0 || x >= (int)Size.x || y >= (int)Size.y)
	{
		return 0.0f;
	}
	return InMap[(c * Size.y + (uint)y) * Size.x + (uint)x];
}

[numthreads(TILE, TILE, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 id : SV_DispatchThreadID)
{
	const int2 tileOrigin = int2(gid.xy) * TILE - HALO; // top-left global texel of this group's halo tile
	const uint threadIdx = gtid.y * TILE + gtid.x;      // 0..63
	const uint kR = KernelSize / 2;
	const uint winY = (uint)gtid.y + HALO - kR; // this thread's KxK window origin inside the LDS tile
	const uint winX = (uint)gtid.x + HALO - kR;

	// Per-thread output-channel accumulators, seeded with bias. Held in registers across the input-channel loop
	// so each input channel is read from global memory only once (into LDS), not once per output channel.
	const uint outC = min(OutChannels, (uint)MAX_OUT_CHANNELS);
	float acc[MAX_OUT_CHANNELS];
	for (uint oc = 0; oc < outC; ++oc)
	{
		acc[oc] = Weights[BiasOffset + oc];
	}

	const uint tileTexels = TILE_DIM * TILE_DIM;
	for (uint ic = 0; ic < InChannels; ++ic)
	{
		// Cooperatively load this input channel's 10x10 halo tile into LDS (64 threads, ~2 texels each).
		for (uint t = threadIdx; t < tileTexels; t += TILE * TILE)
		{
			const uint ly = t / TILE_DIM;
			const uint lx = t - ly * TILE_DIM;
			gTile[t] = ReadInClamped(tileOrigin.x + (int)lx, tileOrigin.y + (int)ly, ic);
		}
		GroupMemoryBarrierWithGroupSync(); // tile for channel ic is visible to all threads

		// MAC this channel's window into every output accumulator. All threads hit the barrier below, so gate
		// the math (not the loop) on being an in-range output pixel.
		if (id.x < Size.x && id.y < Size.y)
		{
			for (uint oc = 0; oc < outC; ++oc)
			{
				const uint wBase = WeightOffset + (oc * InChannels + ic) * KernelSize * KernelSize;
				float s = 0.0f;
				[unroll] for (uint ky = 0; ky < KernelSize; ++ky)
				{
					[unroll] for (uint kx = 0; kx < KernelSize; ++kx)
					{
						s += gTile[(winY + ky) * TILE_DIM + (winX + kx)] * Weights[wBase + ky * KernelSize + kx];
					}
				}
				acc[oc] += s;
			}
		}
		GroupMemoryBarrierWithGroupSync(); // don't let the next channel's load overwrite LDS mid-read
	}

	if (id.x >= Size.x || id.y >= Size.y)
	{
		return;
	}

	for (uint oc = 0; oc < outC; ++oc)
	{
		float v = acc[oc];
		if (Activation == 1 && v < 0.0f)
		{
			v = 0.0f;
		}
		OutMap[(oc * Size.y + id.y) * Size.x + id.x] = v;
	}
}
