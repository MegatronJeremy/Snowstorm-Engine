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
	float3 CameraPosition;
	float Exposure; // linear pre-tonemap multiplier (was _Pad0; same 16-byte slot)
	DirectionalLight DirectionalLights[4];
	int LightCount;
	float3 _Pad1;
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
Texture2D Textures[] : register(t0, space3);
