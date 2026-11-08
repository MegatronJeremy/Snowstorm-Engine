#ifndef SS_DDGI_COMMON_HLSLI
#define SS_DDGI_COMMON_HLSLI

// The constant buffer every DDGI update pass shares (DDGITrace, DDGIBlend*, DDGIBorder). Mirrors DDGICB in
// DDGIPass.cpp; its size is static_asserted there. The hit-shading block satisfies RTHitShading.hlsli's contract.
cbuffer DDGICB : register(b0, space0)
{
	float4x4 RayRotation; // this frame's random rotation of the Fibonacci ray set
	float3 VolumeOrigin;
	float ProbeSpacing;
	uint3 ProbeCounts;
	uint RaysPerProbe;
	uint2 IrradianceAtlasSize;
	uint2 DistanceAtlasSize;
	uint TilesPerRow;
	uint IrradianceAtlasIndex; // bindless, previous frame's atlases for the infinite bounce
	uint DistanceAtlasIndex;
	float Hysteresis;
	float MaxRayDistance;
	float DistanceClamp; // longest distance the moments store, so one far miss cannot dominate a texel
	uint Reset;          // 1: the atlases hold no valid history yet
	uint FrameCounter;

	float3 SunDirection;
	float SunIntensity;
	float3 SunColor;
	float ShadowStrength;
	uint IrradianceCubeIndex;
	uint PrefilteredCubeIndex;
	float IBLIntensity;
	uint LightCount;
	uint GeoTableAddrLo;
	uint GeoTableAddrHi;
	uint HitLightCount;
	uint _Pad0;
	float4 HitLightPosRange[16];
	float4 HitLightColor[16];
	float4 HitLightDirCos[16];
};

#include "DDGI.hlsli"

DDGIVolume CurrentVolume()
{
	DDGIVolume v;
	v.Origin = VolumeOrigin;
	v.Spacing = ProbeSpacing;
	v.Counts = ProbeCounts;
	v.TilesPerRow = TilesPerRow;
	return v;
}

float3 RayDirection(uint ray)
{
	return normalize(mul((float3x3)RayRotation, DDGISphericalFibonacci(ray, RaysPerProbe)));
}

#endif
