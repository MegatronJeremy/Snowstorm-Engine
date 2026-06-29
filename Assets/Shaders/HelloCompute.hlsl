// Compute self-test (Phases 2-3): writes a UV gradient into a storage image (UAV). Proves the compute
// path end to end — pipeline create/bind/dispatch (Phase 2) plus storage-image binding + the
// GENERAL-layout transition and barriers (Phase 3) — all under Vulkan validation, headlessly.

#type compute

RWTexture2D<float4> OutImage : register(u0, space0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	OutImage.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
	{
		return;
	}
	const float2 uv = float2(id.xy) / float2(width, height);
	OutImage[id.xy] = float4(uv, 0.0, 1.0);
}
