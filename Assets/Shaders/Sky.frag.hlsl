#include "Include/Engine.hlsli"
#include "Include/SkyCommon.hlsli"

// Procedural sky background, fragment stage. Paired with Fullscreen.vert.hlsl (drawn after opaque
// geometry at the far plane, depth test LessOrEqual + no depth write, so it only fills uncovered
// pixels). Interim stand-in for a real cubemap skybox (#35) / IBL (#52); reuses the same
// FrameCB.InvViewProj ray reconstruction those will need.

// Tonemap + sRGB encode moved to the post-process pass (Tonemap.frag.hlsl, #53). The sky writes raw
// linear radiance into the HDR scene target, which the post pass then tonemaps alongside the meshes.

// Must match Fullscreen.vert.hlsl's FullscreenVSOut (SV_Position + NDC).
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
	const float3 sky = EvaluateSky(ray, SkyZenithColor, SkyHorizonColor, GroundColor, toSun, sunColor);

	// Output raw linear radiance; the post-process pass (Tonemap.frag.hlsl, #53) applies exposure/ACES/sRGB.
	return float4(sky, 1.0);
}
