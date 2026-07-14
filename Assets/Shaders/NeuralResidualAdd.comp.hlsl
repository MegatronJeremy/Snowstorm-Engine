// Neural upscaler output stage (#47): output = bilinear base + conv-stack residual. Reads the upsampled base
// feature map (CHW channels 0..2) and the final conv layer's output (the residual, CHW channels 0..2), sums
// them, and writes the full-res storage output the tonemap consumes. With an identity (zero output layer)
// network the residual is 0, so output == the bilinear base — the correctness oracle for the whole chain.

StructuredBuffer<float> BaseMap : register(t0, space0);     // bilinear-upsampled input feature map (CHW)
StructuredBuffer<float> ResidualMap : register(t1, space0); // final conv layer output, the residual (CHW)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutImage : register(u2, space0);

cbuffer AddCB : register(b3, space0)
{
	uint2 OutSize;
	uint2 _Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}
	const uint plane = OutSize.x * OutSize.y;
	const uint pix = id.y * OutSize.x + id.x;

	const float3 base = float3(BaseMap[0 * plane + pix], BaseMap[1 * plane + pix], BaseMap[2 * plane + pix]);
	const float3 res = float3(ResidualMap[0 * plane + pix], ResidualMap[1 * plane + pix], ResidualMap[2 * plane + pix]);
	OutImage[id.xy] = float4(base + res, 1.0f);
}
