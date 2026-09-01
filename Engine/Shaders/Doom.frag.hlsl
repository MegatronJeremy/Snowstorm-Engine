#include "Include/Engine.hlsli"

// Presents the embedded Doom framebuffer (DoomSystem uploads it into this material's albedo slot).
// Unlit on purpose: it is a CRT, not a surface, so scene lighting has no business dimming it.

float4 main(PSInput i) : SV_Target0
{
	// Mesh.vert always writes InstanceID, and dxc drops an unread member from the fragment input
	// signature, leaving the pipeline with a vertex output nothing consumes (a validation warning on
	// every pipeline creation). Reading it keeps the interface matched; the branch never runs.
	if (i.InstanceID == 0xFFFFFFFFu)
	{
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	// Index 0 is the "no texture" sentinel (slot 0 is the engine's default white), so this is what the
	// quad shows until DoomSystem takes the albedo over: black, like a screen that is off.
	if (AlbedoTextureIndex == 0)
	{
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	// No V flip: MeshLibrary.cpp stores 1-v at import, so V=0 is the quad's TOP edge, and stb loads
	// texel row 0 as the image top. Doom also writes row 0 at the top, so the two already agree.
	//
	// SampleBindless lives in DefaultLit.frag.hlsl, not the shared header, so the fetch is spelled out
	// here. NonUniformResourceIndex is still mandatory: instanced draws sample garbage without it.
	// SampleLevel(0) rather than SampleBias: the texture is a single mip, and a 640x400 image on a quad
	// wants nearest-to-source texels, not a TAA mip bias meant for scene geometry.
	const float3 screen = Textures[NonUniformResourceIndex(AlbedoTextureIndex)].SampleLevel(ClampSampler, i.TexCoord, 0).rgb;
	return float4(screen, 1.0);
}
