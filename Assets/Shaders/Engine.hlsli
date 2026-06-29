// Engine.hlsli - The Unified Pipeline Layout

// --- Common structs ---
struct DirectionalLight
{
	float3 Direction;
	float Intensity;
	float3 Color;
	float Padding;
};

struct VSInput
{
	float3 Position : TEXCOORD0;
	float3 Normal : TEXCOORD1;
	float2 TexCoord : TEXCOORD2;
	float4 Tangent : TEXCOORD3; // xyz = tangent, w = bitangent handedness sign (glTF/assimp convention)
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

// --- SPACE 0: Global Frame Data ---
cbuffer FrameCB : register(b0, space0)
{
	float4x4 ViewProj;
	float4x4 InvViewProj; // world-ray reconstruction for the sky pass
	float3 CameraPosition;
	float Exposure; // linear pre-tonemap multiplier (was _Pad0; same 16-byte slot)
	DirectionalLight DirectionalLights[4];
	int LightCount;
	float3 _Pad1;

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
	uint ShadowSoft; // 1 = 3x3 PCF, 0 = hard single tap
	float _ShadowPad0;
	float _ShadowPad1;
	float _ShadowPad2;
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
	uint _MatPad0;

	float3 EmissiveColor;
	float _MatPad1;
};
SamplerState LinearSampler : register(s1, space1);

// --- SPACE 2: Per-instance Object Data ---
// One entry per instance, indexed by SV_InstanceID. Lets a single instanced DrawIndexed draw N
// objects that share a mesh+material, with per-object transform / albedo / extras. Layout must match
// the C++ InstanceData struct in RendererSingleton exactly.
struct InstanceData
{
	float4x4 Model;
	uint AlbedoTextureIndex; // per-instance albedo override (0 = use material default)
	float3 _Pad0;
	float4 Extras0;
};
StructuredBuffer<InstanceData> Instances : register(t0, space2);

// --- SPACE 3: Global Bindless Pool ---
// Two parallel arrays in the same set: 2D textures (binding 0) and cubemaps (binding 1). Cube views
// can't share the Texture2D[] array (distinct HLSL types), so IBL env/irradiance/prefilter cubemaps
// are indexed into Cubemaps[] by a separate bindless index (see VulkanBindlessManager::RegisterCube).
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);
