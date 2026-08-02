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
// TWO PATHS, gated on SS_FP16 (device shaderFloat16 + 16-bit storage) — this is deliberate, backed by
// measurement (Debug -Od shaders, temporal upscaler @0.5, 48-ch model, NeuralUpscale GPU ms):
//   * SS_FP16 -> TILED: cache one input channel's halo tile in groupshared, loop input channels accumulating
//     into a per-thread OUTPUT-channel register array (acc[]), so each input texel is read from global memory
//     once per group per channel (not once per output channel). fp16 weights halve the dominant weight load
//     AND keep acc[] in registers. Measured 195ms (naive) -> 84ms.
//   * else -> NAIVE: the simple per-pixel loop nest. On the fp32 fallback the acc[] register array SPILLS to
//     global memory under -Od (measured 392ms, 2x SLOWER than naive), so fp32 devices keep the naive path
//     (195ms) instead. fp16 alone on the naive shader does nothing (196ms — it's input-read-bound, not
//     weight-bound), which is why the tiling is what unlocks the win.
// The tiled inner (oc,k) MAC over the LDS window is the register-GEMM row a cooperative-matrix (tensor-core)
// rewrite swaps in later (#137).

#ifdef SS_FP16
StructuredBuffer<float16_t> Weights : register(t1, space0); // [outC*inC*k*k] then [outC] bias at BiasOffset (fp16)
#else
StructuredBuffer<float> Weights : register(t1, space0);     // [outC*inC*k*k] then [outC] bias at BiasOffset
#endif
StructuredBuffer<float> InMap : register(t0, space0);   // CHW, InChannels*H*W floats
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

// Read input channel c at (x,y); zero outside the image (same-padding border).
float ReadInClamped(int x, int y, uint c)
{
	if (x < 0 || y < 0 || x >= (int)Size.x || y >= (int)Size.y)
	{
		return 0.0f;
	}
	return InMap[(c * Size.y + (uint)y) * Size.x + (uint)x];
}

#ifdef SS_FP16

// 8x8 output tile per group, 1-texel halo (max KernelSize is 3 -> radius 1) -> 10x10 cached tile. ONE input
// channel cached at a time: 100 floats = 400 B LDS, negligible -> full occupancy (caching all channels at once
// was ~25 KB and collapsed occupancy, measured slower). MAX_OUT_CHANNELS caps the per-thread output-accumulator
// register array; 64 covers the trained models (the "big" temporal one is 48-wide) with headroom. MUST match
// kMaxTileChannels in NeuralUpscalePass::EnsureModel (which falls back to identity past it).
#define TILE 8
#define HALO 1
#define TILE_DIM (TILE + 2 * HALO) // 10
#define MAX_OUT_CHANNELS 64

groupshared float gTile[TILE_DIM * TILE_DIM]; // current input channel's halo tile

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
		acc[oc] = (float)Weights[BiasOffset + oc];
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

		// MAC this channel's window into every output accumulator. All threads hit the barriers, so gate the
		// math (not the loop) on being an in-range output pixel.
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
						s += gTile[(winY + ky) * TILE_DIM + (winX + kx)] * (float)Weights[wBase + ky * KernelSize + kx];
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

#else // fp32 fallback: the naive per-pixel loop. The tiled acc[] path above spills its register array under
      // -Od at fp32 (measured 2x slower than naive), so non-fp16 devices use this simpler, faster-at-fp32 path.

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Size.x || id.y >= Size.y)
	{
		return;
	}

	const int x = (int)id.x;
	const int y = (int)id.y;
	const int kR = (int)(KernelSize / 2);

	for (uint oc = 0; oc < OutChannels; ++oc)
	{
		float acc = Weights[BiasOffset + oc]; // bias
		for (uint ic = 0; ic < InChannels; ++ic)
		{
			for (uint ky = 0; ky < KernelSize; ++ky)
			{
				for (uint kx = 0; kx < KernelSize; ++kx)
				{
					const float iv = ReadInClamped(x + (int)kx - kR, y + (int)ky - kR, ic);
					const uint wIdx = WeightOffset + ((oc * InChannels + ic) * KernelSize + ky) * KernelSize + kx;
					acc += iv * Weights[wIdx];
				}
			}
		}
		if (Activation == 1 && acc < 0.0f)
		{
			acc = 0.0f;
		}
		OutMap[(oc * Size.y + (uint)y) * Size.x + (uint)x] = acc;
	}
}

#endif
