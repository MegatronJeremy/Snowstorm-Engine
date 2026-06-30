// Diffuse irradiance convolution: integrate the environment cubemap over the cosine-weighted
// hemisphere around each output direction, producing a small irradiance cubemap sampled for diffuse
// IBL. Dispatched once per face. Standard Lambertian convolution (LearnOpenGL / Karis). Second stage
// of the IBL bake (#52).

#type compute

static const float PI = 3.14159265359;

cbuffer IrradianceParams : register(b0, space0)
{
	uint FaceIndex; // which irradiance face this dispatch writes (0..5)
	float _p0;
	float _p1;
	float _p2;
}

// Bake passes bind their single input cube DIRECTLY in set 0 (not via the runtime bindless array),
// so the reflection-built compute pipeline layout stays self-contained (set 0 only).
TextureCube EnvCube : register(t1, space0);
SamplerState EnvSampler : register(s2, space0);
// rgba16f matches the IBL cube texture format (RGBA16_SFloat); see IBLCapture.hlsl for why this is explicit.
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutFace : register(u3, space0);

float3 CubeFaceDirection(uint face, float2 uv)
{
	float3 dir;
	switch (face)
	{
	case 0: dir = float3(1.0, -uv.y, -uv.x); break;
	case 1: dir = float3(-1.0, -uv.y, uv.x); break;
	case 2: dir = float3(uv.x, 1.0, uv.y); break;
	case 3: dir = float3(uv.x, -1.0, -uv.y); break;
	case 4: dir = float3(uv.x, -uv.y, 1.0); break;
	default: dir = float3(-uv.x, -uv.y, -1.0); break;
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

	const float2 uv = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
	const float3 N = CubeFaceDirection(FaceIndex, uv);

	// Build a tangent basis around N.
	float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	float3 irradiance = float3(0, 0, 0);
	float sampleCount = 0.0;
	const float dPhi = 0.025;
	const float dTheta = 0.025;

	[loop] for (float phi = 0.0; phi < 2.0 * PI; phi += dPhi)
	{
		[loop] for (float theta = 0.0; theta < 0.5 * PI; theta += dTheta)
		{
			// Spherical (tangent space) -> world; cosine-weighted by sin*cos below.
			const float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			const float3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

			irradiance += EnvCube.SampleLevel(EnvSampler, sampleDir, 0).rgb * cos(theta) * sin(theta);
			sampleCount += 1.0;
		}
	}

	irradiance = PI * irradiance / max(sampleCount, 1.0);
	OutFace[id.xy] = float4(irradiance, 1.0);
}
