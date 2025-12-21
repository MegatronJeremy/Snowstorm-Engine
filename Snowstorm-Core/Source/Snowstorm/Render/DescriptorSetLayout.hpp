#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <string>
#include <vector>

namespace Snowstorm
{
	enum class DescriptorType : uint8_t
	{
		UniformBuffer = 0,
		UniformBufferDynamic, // dynamic offset at bind time
		StorageBuffer,
		SampledImage,     // SRV
		StorageImage,     // UAV
		Sampler,          // separate sampler (optional)
		CombinedImageSampler // for APIs/backends that prefer combined bindings
	};

	enum class ShaderStage : uint8_t
	{
		None     = 0,
		Vertex   = 1u << 0,
		Fragment = 1u << 1,
		Compute  = 1u << 2,
		AllGraphics = Vertex | Fragment,
		All = Vertex | Fragment | Compute
	};

	constexpr ShaderStage operator|(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr bool HasStage(ShaderStage value, ShaderStage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	struct DescriptorBindingDesc
	{
		uint32_t Binding = 0;
		DescriptorType Type = DescriptorType::UniformBuffer;
		uint32_t Count = 1;               // arrays: e.g. 32 textures
		ShaderStage Visibility = ShaderStage::AllGraphics;
		std::string DebugName;
	};

	struct DescriptorSetLayoutDesc
	{
		uint32_t SetIndex = 0;
		std::vector<DescriptorBindingDesc> Bindings;
		std::string DebugName;
	};

	class DescriptorSetLayout
	{
	public:
		virtual ~DescriptorSetLayout() = default;

		[[nodiscard]] virtual const DescriptorSetLayoutDesc& GetDesc() const = 0;

		static Ref<DescriptorSetLayout> Create(const DescriptorSetLayoutDesc& desc);

	protected:
		DescriptorSetLayout() = default;
	};
}
