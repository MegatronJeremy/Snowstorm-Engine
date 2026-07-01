#include "Include/SkyCommon.hlsli"

// Capture the analytic procedural sky into one face of an HDR environment cubemap (linear HDR, no
// tonemap). Dispatched once per face: the host binds a per-face UAV (a 2D view of the cube face) and
// sets FaceIndex. Reconstructs the world direction for each texel from the cube-face basis, evaluates
// the shared EvaluateSky, and writes it. First stage of the IBL bake (#52).


cbuffer IBLParams : register(b0, space0)
{
	float3 SkyZenithColor;
	float _p0;
	float3 SkyHorizonColor;
	float _p1;
	float3 GroundColor;
	float _p2;
	float3 ToSun;
	float _p3;
	float3 SunColor;
	uint FaceIndex; // which cube face this dispatch writes (0..5)
}

// rgba16f matches the IBL cube texture format (RGBA16_SFloat); without the explicit format the SPIR-V
// declares the default Rgba32f, which mismatches the bound R16G16B16A16 view (validation warns + UB).
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutFace : register(u1, space0);

// World direction for cube face `face` at face-UV in [-1,1]. Matches the standard D3D/Vulkan cube
// face convention (+X,-X,+Y,-Y,+Z,-Z).
float3 CubeFaceDirection(uint face, float2 uv)
{
	float3 dir;
	switch (face)
	{
	case 0: dir = float3(1.0, -uv.y, -uv.x); break; // +X
	case 1: dir = float3(-1.0, -uv.y, uv.x); break; // -X
	case 2: dir = float3(uv.x, 1.0, uv.y); break;   // +Y
	case 3: dir = float3(uv.x, -1.0, -uv.y); break; // -Y
	case 4: dir = float3(uv.x, -uv.y, 1.0); break;  // +Z
	default: dir = float3(-uv.x, -uv.y, -1.0); break; // -Z
	}
	return normalize(dir);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	OutFace.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
	{
		return;
	}

	// Pixel center -> [0,1] -> [-1,1] face UV.
	const float2 uv = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
	const float3 dir = CubeFaceDirection(FaceIndex, uv);

	const float3 color = EvaluateSky(dir, SkyZenithColor, SkyHorizonColor, GroundColor, ToSun, SunColor);
	OutFace[id.xy] = float4(color, 1.0);
}
