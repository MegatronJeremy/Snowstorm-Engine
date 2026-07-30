// Depth+normal prepass, vertex stage (#124). Renders scene geometry into a partial G-buffer (world
// normal color + sampled depth) BEFORE the forward pass, giving the half-res RT GI compute pass a
// per-pixel world-position (reconstructed from depth) + world-normal source that a forward renderer
// otherwise lacks. Like the shadow/velocity passes, it includes ONLY the minimal mesh interface
// (VSInput + set-2 Instances), NOT Engine.hlsli — so it never drags FrameCB (set 0), the material set
// (set 1) or the bindless arrays (set 3) into its pipeline layout. The camera view-projection travels
// as a 64-byte PUSH CONSTANT (mirrors Shadow.vert), so RendererService::DrawBatchesDepthOnly drives it.
// Paired with DepthNormal.frag.hlsl.
#include "Include/MeshInput.hlsli"

// Combined push constant (both stages): the VS reads ViewProj; the FS reads the alpha-mask fields. One
// struct, one 80-byte range visible to VERTEX|FRAGMENT — avoids sharing MaterialInstance's descriptor set
// (whose set-1 layout differs from this pass's, which caused a pipeline-layout-incompatibility device loss).
// The albedo index rides bindless (set 3), so the per-batch material data is just these 3 scalars here.
struct DepthNormalPush
{
	float4x4 ViewProj;
	uint AlbedoTextureIndex; // bindless index (0 = no albedo -> no clip)
	uint AlphaMaskEnabled;   // 1 = alpha-cutout (glTF MASK)
	float AlphaCutoff;       // albedo.a threshold
	float BaseAlpha;         // material BaseColor.a (multiplies the sampled alpha)
};
[[vk::push_constant]] DepthNormalPush gDN;

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position; // clip pos (drives rasterization + depth write)
	float3 NormalWS : TEXCOORD0;     // world-space geometric normal, interpolated
	float2 TexCoord : TEXCOORD1;     // UV for alpha-mask clip in the fragment stage (#124)
};

DepthNormalVSOut main(VSInput i, uint iid : SV_InstanceID)
{
	DepthNormalVSOut o;

	const float4x4 model = Instances[iid].Model;
	const float4 posWS = mul(float4(i.Position, 1.0), model);

	// Normal matrix: treat Model as rigid/affine (mat3(model)), same as Mesh.vert.
	o.NormalWS = normalize(mul(i.Normal, (float3x3)model));
	o.TexCoord = i.TexCoord;
	o.PositionCS = mul(posWS, gDN.ViewProj);
	return o;
}
