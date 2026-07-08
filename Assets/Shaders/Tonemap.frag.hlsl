#include "Include/Engine.hlsli"

// Post-process output transform (#53): sample the linear-HDR scene color and apply exposure -> ACES
// filmic tonemap, writing to the present target. The sRGB ENCODE is done by the hardware on write
// (#79): the present target is an sRGB-format image, so this shader outputs LINEAR and the GPU encodes
// it — no shader gamma curve. This is the single home of the output transform; the mesh (DefaultLit)
// and sky shaders emit raw linear radiance into the HDR scene target, which this pass finishes. Paired
// with Fullscreen.vert.hlsl; the scene color is read from the bindless table via FrameCB.SceneColorIndex.

// ACES filmic tonemap (Narkowicz fit): compress unbounded linear HDR into [0,1] with a filmic
// shoulder/toe, so bright spots roll off instead of clipping flat white. Cheap, no LUT.
float3 TonemapACES(float3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0; // clip-space xy in [-1,1]
};

float4 main(FullscreenVSOut input) : SV_Target
{
	// The scene and present targets are the SAME resolution, so this is a 1:1 texel copy — use Load()
	// (exact integer fetch) instead of Sample(). That avoids needing a SamplerState entirely: the only
	// samplers Engine.hlsli declares live in set 1 (the material set), which this pass does NOT bind
	// (it binds set 0 = Frame and set 3 = bindless only). Referencing LinearSampler here pulls set 1
	// into the pipeline layout, and the unbound set is a validation error / device-lost fault.
	//
	// SV_Position.xy is the pixel center (x+0.5, y+0.5) in the target, which is oriented the same as the
	// scene target — so the integer texel coord is a straight copy (no Y flip). The editor's existing
	// ImGui::Image flip ({0,1},{1,0}) then displays it right-side up, exactly as before this pass existed.
	const int2 texel = int2(input.PositionCS.xy);
	const float3 hdr = Textures[NonUniformResourceIndex(SceneColorIndex)].Load(int3(texel, 0)).rgb;

	// Output LINEAR; the sRGB-format present target hardware-encodes on write (#79).
	const float3 color = TonemapACES(hdr * Exposure);
	return float4(color, 1.0);
}
