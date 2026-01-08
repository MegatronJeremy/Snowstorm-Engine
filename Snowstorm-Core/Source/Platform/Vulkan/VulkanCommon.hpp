#pragma once

#include <functional>
#include <string>

#include "VulkanContext.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// Access to Vulkan handles
	VulkanContext& GetVulkanContext();
	VkInstance GetVulkanInstance();
	VkDevice GetVulkanDevice();
	VkPhysicalDevice GetVulkanPhysicalDevice();
	VkQueue GetGraphicsQueue();
	uint32_t GetGraphicsQueueFamilyIndex();
	VmaAllocator GetAllocator();

	// Engine-level helpers

	// Shared graphics command pool
	// Used for transient command buffers (uploads, short GPU jobs)
	VkCommandPool GetGraphicsCommandPool();

	// For setup / infrequent uploads
	// For per-frame streaming, prefer a frame-level upload context instead
	void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);

	void CmdTransitionImage(
		VkCommandBuffer cmd,
		VkImage image,
		VkImageAspectFlags aspect,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		uint32_t mipLevels,
		uint32_t layers);

	void SetVulkanObjectName(VkDevice device, uint64_t objectHandle, VkObjectType objectType, const char* name);

	inline bool IsDepthFormat(const PixelFormat fmt)
	{
		return fmt == PixelFormat::D32_Float || fmt == PixelFormat::D24_UNorm_S8_UInt;
	}

	inline PixelFormat FromVkFormat(const VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_B8G8R8A8_UNORM: return PixelFormat::RGBA8_UNorm;
		case VK_FORMAT_B8G8R8A8_SRGB: return PixelFormat::RGBA8_sRGB;
		case VK_FORMAT_D32_SFLOAT: return PixelFormat::D32_Float;
		case VK_FORMAT_D24_UNORM_S8_UINT: return PixelFormat::D24_UNorm_S8_UInt;
		default: return PixelFormat::Unknown;
		}
	}

	inline VkFormat ToVkFormat(const PixelFormat fmt)
	{
		switch (fmt)
		{
		case PixelFormat::RGBA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
		case PixelFormat::RGBA8_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
		case PixelFormat::D32_Float: return VK_FORMAT_D32_SFLOAT;
		case PixelFormat::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
		case PixelFormat::Unknown: break;
		}
		return VK_FORMAT_UNDEFINED;
	}

	inline VkImageUsageFlags ToVkUsage(const TextureUsage usage, const PixelFormat fmt)
	{
		VkImageUsageFlags flags = 0;

		if (HasUsage(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (HasUsage(usage, TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (HasUsage(usage, TextureUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (HasUsage(usage, TextureUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (HasUsage(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (HasUsage(usage, TextureUsage::DepthStencil)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		// Sensible default: if you're going to upload data, you almost always want TransferDst
		// (won't override explicit None; it just helps avoid "why can't I copy?" pain).
		if (flags == 0 && !IsDepthFormat(fmt))
		{
			flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}

		return flags;
	}

	inline VkImageAspectFlags ToVkAspect(const TextureAspect aspect, const PixelFormat fmt)
	{
		if (aspect == TextureAspect::Auto)
		{
			if (fmt == PixelFormat::D24_UNorm_S8_UInt) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			if (IsDepthFormat(fmt)) return VK_IMAGE_ASPECT_DEPTH_BIT;
			return VK_IMAGE_ASPECT_COLOR_BIT;
		}

		switch (aspect)
		{
		case TextureAspect::Color: return VK_IMAGE_ASPECT_COLOR_BIT;
		case TextureAspect::Depth: return VK_IMAGE_ASPECT_DEPTH_BIT;
		case TextureAspect::Stencil: return VK_IMAGE_ASPECT_STENCIL_BIT;
		case TextureAspect::DepthStencil: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		case TextureAspect::Auto: break;
		}

		return VK_IMAGE_ASPECT_COLOR_BIT;
	}

	inline VkDescriptorType ToVkDescriptorType(const DescriptorType type)
	{
		switch (type)
		{
		case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
		case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		}
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}

	inline VkShaderStageFlags ToVkShaderStages(const ShaderStage stages)
	{
		VkShaderStageFlags flags = 0;

		if (HasStage(stages, ShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
		if (HasStage(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (HasStage(stages, ShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;

		return flags;
	}

	inline VkPrimitiveTopology ToVkTopology(const PrimitiveTopology topo)
	{
		switch (topo)
		{
		case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		}
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}

	inline VkCullModeFlags ToVkCullMode(const CullMode cull)
	{
		switch (cull)
		{
		case CullMode::None: return VK_CULL_MODE_NONE;
		case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
		case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
		}
		return VK_CULL_MODE_BACK_BIT;
	}

	inline VkFrontFace ToVkFrontFace(const FrontFace face)
	{
		switch (face)
		{
		case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
		}
		return VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}

	inline VkCompareOp ToVkCompareOp(const CompareOp op)
	{
		switch (op)
		{
		case CompareOp::Never: return VK_COMPARE_OP_NEVER;
		case CompareOp::Less: return VK_COMPARE_OP_LESS;
		case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
		case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
		case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
		}
		return VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	inline VkFormat ToVkVertexFormat(const VertexFormat fmt)
	{
		switch (fmt)
		{
		case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
		case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
		case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
		case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;

		case VertexFormat::UInt: return VK_FORMAT_R32_UINT;
		case VertexFormat::UInt2: return VK_FORMAT_R32G32_UINT;
		case VertexFormat::UInt3: return VK_FORMAT_R32G32B32_UINT;
		case VertexFormat::UInt4: return VK_FORMAT_R32G32B32A32_UINT;

		case VertexFormat::UByte4_Norm: return VK_FORMAT_R8G8B8A8_UNORM;

		case VertexFormat::Unknown: break;
		}
		return VK_FORMAT_UNDEFINED;
	}

	inline VkVertexInputRate ToVkInputRate(const VertexInputRate rate)
	{
		switch (rate)
		{
		case VertexInputRate::PerVertex: return VK_VERTEX_INPUT_RATE_VERTEX;
		case VertexInputRate::PerInstance: return VK_VERTEX_INPUT_RATE_INSTANCE;
		}
		return VK_VERTEX_INPUT_RATE_VERTEX;
	}

	inline VkFilter ToVkFilter(const Filter f)
	{
		return (f == Filter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	}

	inline VkSamplerMipmapMode ToVkMipmapMode(const SamplerMipmapMode m)
	{
		return (m == SamplerMipmapMode::Nearest) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}

	inline VkSamplerAddressMode ToVkAddressMode(const SamplerAddressMode a)
	{
		switch (a)
		{
		case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		}
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}


}
