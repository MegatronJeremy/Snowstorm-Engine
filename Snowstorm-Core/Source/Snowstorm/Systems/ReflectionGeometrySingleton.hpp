#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Math/Math.hpp"

#include <cstdint>

namespace Snowstorm
{
	class Buffer;

	// One record per TLAS instance, letting an inline reflection ray resolve a committed hit to a shadeable
	// surface (RT reflections, #118 follow-up). The record carries the hit mesh's vertex/index buffer DEVICE
	// ADDRESSES (the shader reads attributes via vk::RawBufferLoad, same "geometry by address" the BLAS build
	// uses) plus the material's albedo bindless index + base color + world matrix. Indexed by the committed
	// instanceCustomIndex, which TlasBuildSystem stamps in build order (VulkanTlas::Build) — so this array is
	// filled in lockstep with the TLAS instance gather.
	//
	// Layout MUST match the GeoRecord read in DefaultLit.frag.hlsl field-for-field (the shader reads each
	// field by explicit byte offset via vk::RawBufferLoad, so these offsets are the contract). Rows:
	//   [0]  0: VertexAddress(u64)  8: IndexAddress(u64)
	//   [16] 16: AlbedoTextureIndex(u32)  20: AlphaMaskEnabled(u32)  24: AlphaCutoff(f32) + 1 pad u32
	//   [32] BaseColor(vec4)
	//   [48] Model(mat4, 64B)
	// Total 112 bytes (a multiple of the 16-byte struct alignment; no trailing pad). AlphaMaskEnabled/
	// AlphaCutoff reuse two former pad slots (no size change) so an RT any-hit ray can alpha-test cutout
	// (glTF MASK) geometry instead of treating it as solid. Masked instances are FORCE_NON_OPAQUE in the
	// TLAS and the traversal samples the albedo alpha at the candidate UV against AlphaCutoff.
	struct GeometryRecord
	{
		uint64_t VertexAddress = 0; // GPU address of the mesh vertex buffer (stride sizeof(Vertex) = 48)
		uint64_t IndexAddress = 0;  // GPU address of the mesh index buffer (uint32 indices)

		uint32_t AlbedoTextureIndex = 0; // bindless index into Textures[] (0 = none -> use BaseColor only)
		uint32_t AlphaMaskEnabled = 0;   // 1 = alpha-cutout (glTF MASK): alpha-test the RT hit vs AlphaCutoff
		float AlphaCutoff = 0.5f;        // albedo.a threshold for the mask (unused unless AlphaMaskEnabled)
		uint32_t _Pad2 = 0;

		glm::vec4 BaseColor{1.0f}; // material base-color factor (multiplies the sampled albedo)

		glm::mat4 Model{1.0f}; // object->world, for transforming the interpolated hit normal

		// PBR block (#153), APPENDED after Model so every prior offset is byte-identical: the existing RT
		// readers (GI/Reflection/AO/shadow) stop at Model and are unaffected; only the reference path tracer
		// reads this. Emissive is three explicit floats (not glm::vec3) so the layout can't shift with glm's
		// vec3 alignment mode. Keep in lockstep with GeoRecord in RTGeometry.hlsli (144-byte stride).
		uint32_t MetallicRoughnessTextureIndex = 0; // [112] glTF MR: .b metallic, .g roughness (0 = none)
		uint32_t NormalTextureIndex = 0;            // [116] tangent-space normal map (0 = geometric normal)
		uint32_t EmissiveTextureIndex = 0;          // [120] emissive map (0 = EmissiveColor factor only)
		float Metallic = 0.0f;                      // [124] metallic factor (multiplies MR texture .b)
		float Roughness = 1.0f;                     // [128] perceptual roughness factor (multiplies MR texture .g)
		float EmissiveR = 0.0f;                     // [132] emissive color factor, per channel
		float EmissiveG = 0.0f;                     // [136]
		float EmissiveB = 0.0f;                     // [140]
	};
	static_assert(sizeof(GeometryRecord) == 144, "GeometryRecord must be 144 bytes to match the HLSL RawBufferLoad offsets");

	// Holds the GPU buffer of per-instance GeometryRecords + its device address. Written by TlasBuildSystem
	// (in the same gather loop that builds the TLAS), read by RendererService which pushes the address into
	// FrameCB so DefaultLit's reflection trace can resolve hits. Only maintained while RT reflections are
	// active; empty (address 0) otherwise, and the shader falls back to the sky cube.
	class ReflectionGeometrySingleton final : public Singleton
	{
	public:
		Ref<Buffer> Table;         // device-address storage buffer of GeometryRecord[]; grown as needed
		uint32_t Capacity = 0;     // records the current Table can hold
		uint64_t TableAddress = 0; // Table's GPU device address (0 = no table this frame)
	};
}
