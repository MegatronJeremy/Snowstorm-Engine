// MeshInput.hlsli - the minimal vertex-stage interface shared by every mesh-drawing pass: the vertex
// layout (VSInput) and the per-instance object buffer (set 2). Split out of Engine.hlsli so a
// depth-only pass (Shadow.vert) can include JUST what it uses. Including the full Engine.hlsli dragged
// FrameCB (set 0), MaterialCB + samplers (set 1) and the bindless arrays (set 3) into the shadow
// shader's SPIR-V interface, so a graphics debugger reported "shader referenced set 0/1/3 that was not
// bound" on every shadow draw (the shadow pass legitimately binds only set 2). Engine.hlsli includes
// this, so the lit shaders are unchanged.

#ifndef SNOWSTORM_MESH_INPUT_HLSLI
#define SNOWSTORM_MESH_INPUT_HLSLI

struct VSInput
{
	float3 Position : TEXCOORD0;
	float3 Normal : TEXCOORD1;
	float2 TexCoord : TEXCOORD2;
	float4 Tangent : TEXCOORD3; // xyz = tangent, w = bitangent handedness sign (glTF/assimp convention)
};

// --- SPACE 2: Per-instance Object Data ---
// One entry per instance, indexed by SV_InstanceID. Lets a single instanced DrawIndexed draw N
// objects that share a mesh+material, with per-object transform / albedo / extras. Layout must match
// the C++ InstanceData struct in RendererSingleton exactly.
struct InstanceData
{
	float4x4 Model;
	float4x4 PrevModel;      // last frame's world matrix -- for motion vectors (#44)
	uint AlbedoTextureIndex; // per-instance albedo override (0 = use material default)
	float3 _Pad0;
	float4 Extras0;
};
StructuredBuffer<InstanceData> Instances : register(t0, space2);

#endif // SNOWSTORM_MESH_INPUT_HLSLI
