#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <string>
#include <vector>

#include "RenderEnums.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"

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

	struct DescriptorBindingDesc
	{
		uint32_t Binding = 0;
		DescriptorType Type = DescriptorType::UniformBuffer;
		uint32_t Count = 1;               // arrays: e.g. 32 textures
		ShaderStage Visibility = ShaderStage::AllGraphics;
		bool IsBindless = false;
		std::string DebugName;
	};

	struct DescriptorSetLayoutDesc
	{
		uint32_t SetIndex = 0;
		std::vector<DescriptorBindingDesc> Bindings;
		std::string DebugName;
	};

	class DescriptorSetLayout : public NonCopyable
	{
	public:
		virtual ~DescriptorSetLayout() = default;

		[[nodiscard]] virtual const DescriptorSetLayoutDesc& GetDesc() const = 0;

		static Ref<DescriptorSetLayout> Create(const DescriptorSetLayoutDesc& desc);
		static Ref<DescriptorSetLayout> CreateFromExternal(void* internalHandle);

	protected:
		DescriptorSetLayout() = default;
	};
}
