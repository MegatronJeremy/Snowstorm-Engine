// DescriptorSet.hpp
#pragma once

#include "Snowstorm/Render/DescriptorSetLayout.hpp"

#include "Snowstorm/Core/Base.hpp"

#include <cstdint>
#include <string>

namespace Snowstorm
{
	class Buffer;
	class TextureView;
	class Sampler;

	struct DescriptorSetDesc
	{
		// Which set index this corresponds to in the pipeline layout.
		std::string DebugName;
	};

	struct BufferBinding
	{
		Ref<Buffer> Buffer;
		uint64_t Offset = 0;
		uint64_t Range = 0; // 0 = whole buffer (backend decides)
	};

	// Descriptor set abstraction (maps to VkDescriptorSet / D3D12 descriptor tables).
	class DescriptorSet 
	{
	public:
		virtual ~DescriptorSet() = default;

		[[nodiscard]] virtual const DescriptorSetDesc& GetDesc() const = 0;
		[[nodiscard]] virtual const Ref<DescriptorSetLayout>& GetLayout() const = 0;

		// Update binding contents (no GPU submission implied; backend may batch writes)
		virtual void SetBuffer(uint32_t binding, const BufferBinding& buffer, uint32_t arrayIndex = 0) = 0;
		virtual void SetTexture(uint32_t binding, const Ref<TextureView>& textureView, uint32_t arrayIndex = 0) = 0;
		virtual void SetSampler(uint32_t binding, const Ref<Sampler>& sampler, uint32_t arrayIndex = 0) = 0;

		// Push writes to the backend (VkUpdateDescriptorSets / rebuild descriptor table, etc.)
		virtual void Commit() = 0;

		static Ref<DescriptorSet> Create(const Ref<DescriptorSetLayout>& layout, const DescriptorSetDesc& desc);

	protected:
		DescriptorSet() = default;
	};
}
