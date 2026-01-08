#include "MaterialInstance.hpp"

#include <entt/entity/entity.hpp>

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMaterialConstantsBinding = 0;
		constexpr uint32_t kMaterialSamplerBinding   = 1;

		uint32_t s_NumFrames = 2; // TODO this is somewhere in Renderer...
	}

	MaterialInstance::MaterialInstance(const Ref<Material>& baseMaterial)
		: m_Base(baseMaterial)
	{
		SS_CORE_ASSERT(m_Base, "MaterialInstance requires a base Material");

		// 1. Copy default constants from base (includes the AlbedoTextureIndex)
		m_Constants = m_Base->GetDefaultConstants();
		m_Sampler = m_Base->GetSampler();
		m_AlbedoTexture = m_Base->GetAlbedoTexture();

		// 2. Setup layout
		const auto& setLayouts = m_Base->GetPipeline()->GetSetLayouts();
		m_SetLayout = setLayouts[1];

		m_DirtyFramesCounter = 2; 
	}

	const Ref<Pipeline>& MaterialInstance::GetPipeline() const
	{
		return m_Base->GetPipeline();
	}

	void MaterialInstance::SetAlbedoTexture(const Ref<TextureView>& view)
	{
		m_AlbedoTexture = view;
		m_Constants.AlbedoTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		m_DirtyFramesCounter = 2;
	}

	void MaterialInstance::SetNormalTexture(const Ref<TextureView>& view)
	{
		// Even if your shader doesn't use it yet, your C++ struct is ready
		m_Constants.NormalTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		m_DirtyFramesCounter = 2;
	}

	void MaterialInstance::SetBaseColor(const glm::vec4& color)
	{
		m_Constants.BaseColor = color;
		m_DirtyFramesCounter = 2;
	}

	void MaterialInstance::SetSampler(const Ref<Sampler>& sampler)
	{
		m_Sampler = sampler;
	}

	void MaterialInstance::EnsurePerFrameResources(const uint32_t frameIndex)
	{
		if (m_MaterialDataSets.size() <= frameIndex)
		{
			m_MaterialDataSets.resize(frameIndex + 1);
			m_UniformBuffers.resize(frameIndex + 1);
		}

		if (!m_MaterialDataSets[frameIndex])
		{
			m_UniformBuffers[frameIndex] = Buffer::Create(sizeof(Material::Constants), BufferUsage::Uniform, nullptr, true, "MaterialInstance_UniformBuffer");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "MaterialInstance_DataSet";
			m_MaterialDataSets[frameIndex] = DescriptorSet::Create(m_SetLayout, setDesc);

			// Permanent bindings for this material's data
			const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(Material::Constants)};
			m_MaterialDataSets[frameIndex]->SetBuffer(kMaterialConstantsBinding, bb);
			m_MaterialDataSets[frameIndex]->SetSampler(kMaterialSamplerBinding, m_Sampler);
		}
	}

	void MaterialInstance::UpdateGPU(const uint32_t frameIndex)
	{
		EnsurePerFrameResources(frameIndex);

		// Keep track of how many frames still need an update
		if (m_DirtyFramesCounter > 0)
		{
			m_UniformBuffers[frameIndex]->SetData(&m_Constants, sizeof(Material::Constants), 0);

			// Re-bind just in case, then Commit to GPU
			const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(Material::Constants)};
			m_MaterialDataSets[frameIndex]->SetBuffer(kMaterialConstantsBinding, bb);
			m_MaterialDataSets[frameIndex]->SetSampler(kMaterialSamplerBinding, m_Sampler);

			m_MaterialDataSets[frameIndex]->Commit();

			// Only decrement on the last step of the logic, or use a per-frame dirty bitmask
			// For now, we'll just decrement. (Note: in a heavy engine, you'd use a bitmask)
			m_DirtyFramesCounter--; 
		}
	}

	void MaterialInstance::Apply(CommandContext& ctx, const uint32_t frameIndex)
	{
		UpdateGPU(frameIndex);

		ctx.BindPipeline(m_Base->GetPipeline());

		// Bind this material's unique data (Set 1) 
		ctx.BindDescriptorSet(m_MaterialDataSets[frameIndex], 1); 
	}
}