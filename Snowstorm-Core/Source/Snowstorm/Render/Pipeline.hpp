// Pipeline.hpp
#pragma once

#include "Shader.hpp"
#include "Snowstorm/Core/Base.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "DescriptorSetLayout.hpp"
#include "RenderEnums.hpp"

namespace Snowstorm
{
	enum class PipelineType : uint8_t
	{
		Graphics = 0,
		Compute  = 1
	};

	enum class PrimitiveTopology : uint8_t
	{
		TriangleList = 0,
		TriangleStrip,
		LineList,
		LineStrip,
	};

	enum class CullMode : uint8_t
	{
		None = 0,
		Front,
		Back,
	};

	enum class FrontFace : uint8_t
	{
		CounterClockwise = 0,
		Clockwise
	};

	// Push constants (Vulkan/DX12-friendly)
	enum class PipelineShaderStage : uint32_t
	{
		None     = 0,
		Vertex   = 1u << 0,
		Fragment = 1u << 1,
		Compute  = 1u << 2,
		AllGraphics = Vertex | Fragment,
		All = Vertex | Fragment | Compute,
	};

	constexpr PipelineShaderStage operator|(PipelineShaderStage a, PipelineShaderStage b)
	{
		return static_cast<PipelineShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr bool HasStage(PipelineShaderStage value, PipelineShaderStage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	struct PushConstantRangeDesc
	{
		// Byte offset in push constant block
		uint32_t Offset = 0;

		// Byte size of the range
		uint32_t Size = 0;

		// Shader stages that can access this range
		PipelineShaderStage Stages = PipelineShaderStage::AllGraphics;
	};

	// Minimal but Vulkan/DX12-friendly: explicit formats for dynamic rendering / RTV/DSV.
	enum class PipelineFormat : uint8_t
	{
		Unknown = 0,

		RGBA8_UNorm,
		RGBA8_sRGB,

		D32_Float,
		D24_UNorm_S8_UInt,
	};

	// --- Vertex layout (for mesh rendering) ---
	enum class VertexInputRate : uint8_t
	{
		PerVertex = 0,
		PerInstance = 1
	};

	enum class VertexFormat : uint8_t
	{
		Unknown = 0,

		Float,   // 1x32-bit
		Float2,  // 2x32-bit
		Float3,  // 3x32-bit
		Float4,  // 4x32-bit

		UInt,    // 1x32-bit
		UInt2,
		UInt3,
		UInt4,

		UByte4_Norm, // e.g. RGBA color packed
	};

	struct VertexAttributeDesc
	{
		// Matches shader `layout(location = X)`
		uint32_t Location = 0;

		VertexFormat Format = VertexFormat::Unknown;

		// Byte offset from the start of the vertex struct
		uint32_t Offset = 0;
	};

	struct VertexBufferLayoutDesc
	{
		// Which binding slot this vertex buffer is bound to (vkCmdBindVertexBuffers(binding, ...))
		uint32_t Binding = 0;

		VertexInputRate InputRate = VertexInputRate::PerVertex;

		// Byte stride of one vertex (sizeof(Vertex))
		uint32_t Stride = 0;

		// Attributes sourced from this binding
		std::vector<VertexAttributeDesc> Attributes;
	};

	struct VertexLayoutDesc
	{
		// Multiple bindings allow interleaved or separate streams (positions in binding0, uvs in binding1, etc.)
		std::vector<VertexBufferLayoutDesc> Buffers;

		[[nodiscard]] bool IsEmpty() const { return Buffers.empty(); }
	};

	struct PipelineDepthStencilState
	{
		bool EnableDepthTest  = false;
		bool EnableDepthWrite = false;
		CompareOp DepthCompare = CompareOp::LessOrEqual;

		bool EnableStencil = false;
	};

	struct PipelineRasterState
	{
		PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
		CullMode Cull = CullMode::Back;
		FrontFace Front = FrontFace::CounterClockwise;

		bool Wireframe = false;
	};

	struct PipelineBlendAttachment
	{
		bool EnableBlend = false;
	};

	struct PipelineBlendState
	{
		std::vector<PipelineBlendAttachment> Attachments;
	};

	struct PipelineDesc
	{
		PipelineType Type = PipelineType::Graphics;

		Ref<Shader> Shader;

		// Mesh input
		VertexLayoutDesc VertexLayout;

		// Dynamic rendering / render target compatibility
		std::vector<PipelineFormat> ColorFormats;
		PipelineFormat DepthFormat = PipelineFormat::Unknown;
		bool HasStencil = false;

		PipelineRasterState Raster{};
		PipelineDepthStencilState DepthStencil{};
		PipelineBlendState Blend{};

		// Push constants (optional)
		std::vector<PushConstantRangeDesc> PushConstants;

		std::string DebugName;
	};

	class Pipeline
	{
	public:
		virtual ~Pipeline() = default;

		[[nodiscard]] virtual const PipelineDesc& GetDesc() const = 0;

		// Modern: pipelines own descriptor set layouts (set 0=material, set 1=frame, etc.)
		[[nodiscard]] virtual const std::vector<Ref<DescriptorSetLayout>>& GetSetLayouts() const = 0;

		static Ref<Pipeline> Create(const PipelineDesc& desc);

	protected:
		Pipeline() = default;
	};
}
