// Engine.hlsli - The Unified Pipeline Layout

// Vertex layout (VSInput) + per-instance object buffer (set 2). Split out so depth-only passes can
// include just that minimal interface; the lit path gets it transitively here.
#include "Include/MeshInput.hlsli"

// --- Common structs ---
struct DirectionalLight
{
	float3 Direction;
	float Intensity;
	float3 Color;
	float Padding;
};

// Positional lights. Mirror GPUPointLight / GPUSpotLight in LightingUniforms.hpp field-for-field.
// Cone angles arrive as cos(angle) so the shader compares against dot() with no trig.
struct PointLight
{
	float3 Position;
	float Range;
	float3 Color;
	float Intensity;
	// Shadow slot: index into FrameCB.PointShadows (0..MAX_SHADOW_POINTS-1), or < 0 when unshadowed.
	int ShadowSlot;
	float3 ShadowPad;
};

// 6-face shadow payload for one shadow-casting point light (cube unrolled into the point atlas). Mirrors
// GPUPointShadow in LightingUniforms.hpp field-for-field. Face order = +X,-X,+Y,-Y,+Z,-Z.
struct PointShadow
{
	float4x4 Face[6];
	float4 Rect[6];
};

struct SpotLight
{
	float3 Position;
	float Range;
	float3 Color;
	float Intensity;
	float3 Direction;
	float CosInner;
	float CosOuter;
	// Shadow: ShadowIndex < 0 => no shadow. ShadowViewProj reprojects world -> this spot's light clip;
	// ShadowAtlasRect (xy = UV offset, zw = UV scale) maps that into the spot's tile of the atlas.
	int ShadowIndex;
	float2 ShadowPad;
	float4x4 ShadowViewProj;
	float4 ShadowAtlasRect;
};

struct VSOutput
{
	float4 PositionCS : SV_Position;
	float2 TexCoord : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float3 PositionWS : TEXCOORD2;
	float4 TangentWS : TEXCOORD3; // world-space tangent (xyz) + handedness (w) for normal mapping
	nointerpolation uint InstanceID : TEXCOORD4; // carry SV_InstanceID to the fragment stage
};

struct PSInput
{
	float4 PositionCS : SV_Position;
	float2 TexCoord : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float3 PositionWS : TEXCOORD2;
	float4 TangentWS : TEXCOORD3;
	nointerpolation uint InstanceID : TEXCOORD4;
};

static const int MAX_DIRECTIONAL_LIGHTS = 4;
static const int MAX_POINT_LIGHTS = 16;
static const int MAX_SPOT_LIGHTS = 16;
static const int MAX_SHADOW_POINTS = 2; // hard cap on shadow-casting point lights (6 depth passes each)

// --- SPACE 0: Global Frame Data ---
cbuffer FrameCB : register(b0, space0)
{
	float4x4 ViewProj;
	float4x4 InvViewProj;  // world-ray reconstruction for the sky pass
	float4x4 PrevViewProj; // last frame's VP -- motion vectors (#44); mirrors FrameCB in RendererService.cpp
	float3 CameraPosition;
	float Exposure; // linear pre-tonemap multiplier (was _Pad0; same 16-byte slot)
	DirectionalLight DirectionalLights[4];
	int LightCount;
	float3 _Pad1;

	// Positional lights. Appended after the directional block; mirrors LightDataBlock's tail (each count
	// followed by a float3 pad to keep the next array 16-byte aligned).
	PointLight PointLights[16];
	int PointCount;
	float3 _PointPad;

	SpotLight SpotLights[16];
	int SpotCount;
	float3 _SpotPad;

	// Point (omni) shadow payloads, indexed by PointLight.ShadowSlot. Appended at the light block's tail
	// so no existing offset moves; mirrors LightDataBlock's tail in LightingUniforms.hpp field-for-field.
	PointShadow PointShadows[2]; // MAX_SHADOW_POINTS
	int PointShadowCount;
	float3 _PointShadowPad;

	// Environment: shared by the sky pass and the DefaultLit ambient term. Mirrors the FrameCB tail in
	// RendererSingleton.cpp field-for-field (each float3 register-packed with the trailing float).
	float3 SkyZenithColor;
	float SkyIntensity;
	float3 SkyHorizonColor;
	float _EnvPad0;
	float3 GroundColor;
	float _EnvPad1;

	// Directional shadow (sun). LightViewProj reprojects world -> light clip; ShadowMapIndex is the
	// bindless depth-texture index (0 = no shadows). Mirrors the FrameCB tail in RendererSingleton.cpp.
	float4x4 LightViewProj;
	uint ShadowMapIndex;
	float ShadowBias;
	float ShadowTexelSize;
	float ShadowStrength;
	uint ShadowSoft;            // 1 = 3x3 PCF, 0 = hard single tap
	uint SpotShadowAtlasIndex;  // bindless index of the spot shadow atlas (0 = spots unshadowed)
	uint PointShadowAtlasIndex; // bindless index of the point shadow atlas (0 = points unshadowed)
	float _ShadowPad2;

	// IBL: bindless indices of the baked maps (irradiance + prefiltered in Cubemaps[], BRDF LUT in
	// Textures[]); 0 = IBL off (analytic hemisphere ambient). PrefilteredMipCount maps roughness->lod.
	// Mirrors the FrameCB tail in RendererSingleton.cpp.
	uint IrradianceCubeIndex;
	uint PrefilteredCubeIndex;
	uint BRDFLutIndex;
	uint PrefilteredMipCount;
	float IBLIntensity;
	float _IBLPad0;
	float _IBLPad1;
	float _IBLPad2;

	// Texture mip-LOD bias (#44 TAA): negative under TAA (~-1) so jittered sampling fetches a sharper mip
	// each frame and the temporal accumulation reconstructs detail instead of flickering between mips on
	// thin/distant surfaces; 0 when TAA is off. Consumed by SampleBindless in DefaultLit. Reuses a former
	// reserved pad slot, so the FrameCB layout is unchanged. MUST match the FrameCB in RendererService.cpp.
	float MipBias;
	// Ray-traced sun shadow (#118): 1 = trace the sun's shadow via ray query (SS_RAYTRACING builds only),
	// 0 = sample the raster shadow map. Reuses a former reserved pad slot; MUST match FrameCB in
	// RendererService.cpp (uint there too).
	uint RTShadowEnabled;
	// Ray-traced ambient occlusion (#118): 1 = trace hemisphere occlusion rays and darken ambient
	// (SS_RAYTRACING builds only), 0 = no RTAO. AORadius is the occlusion sample distance in world units.
	// Both reuse former reserved pad slots (layout unchanged).
	uint RTAOEnabled;
	float AORadius;
	// AOIntensity scales the darkening (1 = physical, >1 = artistic boost). FrameCounter is the monotonic
	// frame index (low 32 bits) — the first shader-reachable frame counter; RTAO rotates its sample set by
	// it each frame so TAA averages successive samples into smooth AO. New 16-byte row; MUST match
	// RendererService.cpp field-for-field.
	float AOIntensity;
	uint FrameCounter;
	// Debug: 1 = output the isolated grayscale AO term (material AO * RTAO) instead of the shaded scene, for
	// tuning the RTAO radius/intensity against the raw signal. Reuses a former pad slot.
	uint DebugAO;
	// Soft RT shadows (#118): SunAngularRadius is the sun's angular HALF-size in radians (= ½ angular
	// diameter; real sun ~0.0047 rad). LightSourceRadius is a local light's physical radius in world units.
	// Drive the shadow-ray cone jitter (bigger source => wider penumbra). Consumed only when ShadowSoft != 0
	// in the RT path. SunAngularRadius fills the last pad; LightSourceRadius starts a new 16-byte row.
	float SunAngularRadius;
	float LightSourceRadius;
	// RT reflections (#118): RTReflEnabled gates the reflection trace (SS_RAYTRACING builds + a geometry
	// table present); ReflIntensity scales the contribution; ReflMaxRoughness is the roughness cutoff
	// (smoother = RT, rougher = the prefiltered cube). Reuse the former shadow-soft pad slots.
	uint RTReflEnabled;
	float ReflIntensity;
	float ReflMaxRoughness;
	// GPU device address of the per-instance GeometryRecord table, split lo/hi (see RendererService.cpp).
	// Reassembled to a uint64 in the shader and read with vk::RawBufferLoad to resolve a reflected hit's
	// surface. 0 = no table -> reflection falls back to the sky cube. New 16-byte row; MUST match
	// RendererService.cpp field-for-field.
	uint ReflGeoTableAddrLo;
	uint ReflGeoTableAddrHi;
	float _ReflPad0;
	float _ReflPad1;
};

// --- SPACE 1: Material Data ---
// MUST match Material::Constants in Snowstorm/Render/Material.hpp field-for-field (16-byte rows).
cbuffer MaterialCB : register(b0, space1)
{
	float4 BaseColor;

	uint AlbedoTextureIndex;
	uint NormalTextureIndex;
	float Roughness;
	float Metallic;

	uint MetallicRoughnessTextureIndex; // glTF packing: G = roughness, B = metallic
	uint AOTextureIndex;
	uint EmissiveTextureIndex;
	uint AlphaMaskEnabled; // 1 = alpha-cutout (glTF MASK): discard texels below AlphaCutoff

	float3 EmissiveColor;
	float AlphaCutoff; // albedo.a threshold for the mask (glTF default 0.5); unused unless masked
};
SamplerState LinearSampler : register(s1, space1);
// Clamp-to-edge linear sampler for lookup textures that must not wrap (e.g. the BRDF LUT). Bound at the
// fixed material binding 2; engine-global (every material binds the same one).
SamplerState ClampSampler : register(s2, space1);

// SPACE 2 (per-instance object data: InstanceData + Instances) lives in MeshInput.hlsli, included above.

// --- SPACE 3: Global Bindless Pool ---
// Two parallel arrays in the same set: 2D textures (binding 0) and cubemaps (binding 1). Cube views
// can't share the Texture2D[] array (distinct HLSL types), so IBL env/irradiance/prefilter cubemaps
// are indexed into Cubemaps[] by a separate bindless index (see VulkanBindlessManager::RegisterCube).
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);

// Scene top-level acceleration structure for inline ray query (#118). Bindless set 3, binding 2 (see
// VulkanBindlessManager::BINDING_TLAS). Declared only in the SS_RAYTRACING permutation — on a non-RT
// device the define is absent, so the SPIR-V carries no RayQueryKHR capability (which would need the
// device extension). Written each frame by TlasBuildSystem; unwritten (PARTIALLY_BOUND) before the first
// build, so callers must gate on RTShadowEnabled, which is only set once a scene TLAS exists.
#ifdef SS_RAYTRACING
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);
#endif
