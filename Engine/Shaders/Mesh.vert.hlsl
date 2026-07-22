#include "Include/Engine.hlsli"

// Standard mesh vertex stage: transforms an instanced mesh vertex by its per-instance Model then the
// FrameCB ViewProj, and fills the full VSOutput (world pos/normal/tangent + UV + instance id). Shared
// by every forward material that draws meshes -- DefaultLit and Mandelbrot both pair with this; their
// fragment stages differ, the vertex work is identical (Mandelbrot simply ignores normal/tangent). New
// mesh materials should reuse this rather than duplicating the transform.

VSOutput main(VSInput i, uint iid : SV_InstanceID)
{
	VSOutput o;

	const float4x4 model = Instances[iid].Model;

	const float4 posWS = mul(float4(i.Position, 1.0), model);
	o.PositionWS = posWS.xyz;

	// Normal matrix: treat Model as rigid/affine (mat3(model)).
	o.NormalWS = normalize(mul(i.Normal, (float3x3)model));
	// Tangent to world space (same rigid/affine assumption); keep handedness w untouched.
	o.TangentWS = float4(normalize(mul(i.Tangent.xyz, (float3x3)model)), i.Tangent.w);

	o.TexCoord = i.TexCoord;
	o.PositionCS = mul(posWS, ViewProj);
	o.InstanceID = iid;
	return o;
}
