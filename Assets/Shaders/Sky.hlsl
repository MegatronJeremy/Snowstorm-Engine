#include "Engine.hlsli"
#include "SkyCommon.hlsli"

// Procedural sky background. Drawn after opaque geometry at the far plane: a fullscreen triangle with
// NDC z = 1.0, depth test LessOrEqual, depth write OFF, so it only fills pixels no mesh covered.
// Interim stand-in for a real cubemap skybox (#35) / IBL (#52) — it reuses the same FrameCB.InvViewProj
// ray reconstruction and far-plane pass those will need, so this is a strict subset of that work.

#type vertex

struct SkyVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0; // clip-space xy in [-1,1], for per-pixel ray reconstruction
};

SkyVSOut main(uint vid : SV_VertexID)
{
	SkyVSOut o;
	// Oversized fullscreen triangle from the vertex id (no vertex buffer): covers the whole screen.
	const float2 ndc = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
	o.NDC = ndc;
	// z = w => NDC z = 1.0 after the perspective divide: the far plane, so LessOrEqual keeps the sky
	// behind everything already in the depth buffer.
	o.PositionCS = float4(ndc, 1.0, 1.0);
	return o;
}

#type fragment

// Mirrors DefaultLit's output transform so the sky shares the scene's exposure/tonemap/encode. When
// the post-process pass (#53) centralizes this, both shaders drop their inline copy.
float3 TonemapACES(float3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LinearToSRGB(float3 c)
{
	return pow(max(c, 0.0), 1.0 / 2.2);
}

struct SkyPSIn
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

float4 main(SkyPSIn i) : SV_Target0
{
	// Reconstruct the world-space view ray for this pixel. Unproject a far-plane clip point with
	// InvViewProj, perspective-divide to a world position, then subtract the camera position.
	float4 worldH = mul(float4(i.NDC, 1.0, 1.0), InvViewProj);
	const float3 worldPos = worldH.xyz / worldH.w;
	const float3 ray = normalize(worldPos - CameraPosition);

	// Shared sky evaluation (also used by the IBL capture). Pull the gradient + sun from FrameCB; the
	// sun's toSun is the negated light direction, zeroed when there is no directional light.
	const float3 toSun = (LightCount > 0) ? normalize(-DirectionalLights[0].Direction) : float3(0, 0, 0);
	const float3 sunColor = (LightCount > 0) ? DirectionalLights[0].Color * DirectionalLights[0].Intensity : float3(0, 0, 0);
	float3 sky = EvaluateSky(ray, SkyZenithColor, SkyHorizonColor, GroundColor, toSun, sunColor);

	sky = TonemapACES(sky * Exposure);
	sky = LinearToSRGB(sky);
	return float4(sky, 1.0);
}
