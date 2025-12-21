#include "MaterialInstance.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMaterialConstantsBinding = 0;
		constexpr uint32_t kMaterialTexturesBinding  = 1;
		constexpr uint32_t kMaterialSamplerBinding   = 2; // <-- NEW
	}

	MaterialInstance::MaterialInstance(const Ref<Material>& baseMaterial)
		: m_Base(baseMaterial)
	{
		SS_CORE_ASSERT(m_Base, "MaterialInstance requires a base Material");
		SS_CORE_ASSERT(m_Base->GetPipeline(), "MaterialInstance base material has no pipeline");

		// Base defaults
		m_Constants.BaseColor = m_Base->GetBaseColor();
		m_ObjectExtras0 = glm::vec4(0.0f);

		for (uint32_t i = 0; i < MAX_TEXTURE_SLOTS; ++i)
		{
			m_TextureViews[i] = m_Base->GetTextureView(i);
		}

		m_Sampler = m_Base->GetSampler();

		// Set=1 layout comes from pipeline
		const auto& setLayouts = m_Base->GetPipeline()->GetSetLayouts();
		SS_CORE_ASSERT(setLayouts.size() > 1 && setLayouts[1], "Pipeline missing set=1 Material layout");

		m_SetLayout = setLayouts[1];

		DescriptorSetDesc setDesc{};
		setDesc.DebugName = "MaterialInstanceDescriptorSet";
		m_DescriptorSet = DescriptorSet::Create(m_SetLayout, setDesc);
		SS_CORE_ASSERT(m_DescriptorSet, "Failed to create MaterialInstance descriptor set");
	}

	const Ref<Pipeline>& MaterialInstance::GetPipeline() const
	{
		return m_Base->GetPipeline();
	}

	void MaterialInstance::SetBaseColor(const glm::vec4& color)
	{
		m_Constants.BaseColor = color;
		m_DirtyConstants = true;
	}

	void MaterialInstance::SetTextureView(const uint32_t slot, const Ref<TextureView>& view)
	{
		SS_CORE_ASSERT(slot < MAX_TEXTURE_SLOTS, "Texture slot out of range");
		m_TextureViews[slot] = view;
		m_DirtyTextures = true;
	}

	Ref<TextureView> MaterialInstance::GetTextureView(const uint32_t slot) const
	{
		SS_CORE_ASSERT(slot < MAX_TEXTURE_SLOTS, "Texture slot out of range");
		return m_TextureViews[slot];
	}

	void MaterialInstance::SetSampler(const Ref<Sampler>& sampler)
	{
		m_Sampler = sampler;
		m_DirtyTextures = true;
	}

	void MaterialInstance::EnsurePerFrameBuffers(const uint32_t frameIndex)
	{
		if (m_PerFrameUniformBuffers.size() > frameIndex && m_PerFrameUniformBuffers[frameIndex])
			return;

		if (m_PerFrameUniformBuffers.size() <= frameIndex)
			m_PerFrameUniformBuffers.resize(frameIndex + 1);

		m_PerFrameUniformBuffers[frameIndex] = Buffer::Create(sizeof(Material::Constants), BufferUsage::Uniform, nullptr, true);
		SS_CORE_ASSERT(m_PerFrameUniformBuffers[frameIndex], "Failed to create per-frame MaterialInstance uniform buffer");
	}

	void MaterialInstance::UpdateGPU(const uint32_t frameIndex)
	{
		EnsurePerFrameBuffers(frameIndex);

		if (m_DirtyConstants)
		{
			m_PerFrameUniformBuffers[frameIndex]->SetData(&m_Constants, sizeof(Material::Constants), 0);

			BufferBinding bb{};
			bb.Buffer = m_PerFrameUniformBuffers[frameIndex];
			bb.Offset = 0;
			bb.Range  = sizeof(Material::Constants);
			m_DescriptorSet->SetBuffer(kMaterialConstantsBinding, bb);
		}

		if (m_DirtyTextures)
		{
			SS_CORE_ASSERT(m_Sampler, "MaterialInstance sampler is null");

			for (uint32_t i = 0; i < MAX_TEXTURE_SLOTS; i++)
			{
				if (!m_TextureViews[i])
					continue;

				// Textures array: set=1 binding=1 [i]
				m_DescriptorSet->SetTexture(kMaterialTexturesBinding, m_TextureViews[i], i);
			}

			// Single sampler: set=1 binding=2 [0]
			m_DescriptorSet->SetSampler(kMaterialSamplerBinding, m_Sampler, 0);
		}

		if (m_DirtyConstants || m_DirtyTextures)
		{
			m_DescriptorSet->Commit();
			m_DirtyConstants = false;
			m_DirtyTextures = false;
		}
	}

	void MaterialInstance::Apply(CommandContext& ctx, const uint32_t frameIndex)
	{
		UpdateGPU(frameIndex);

		ctx.BindPipeline(m_Base->GetPipeline());
		ctx.BindDescriptorSet(m_DescriptorSet, m_SetLayout->GetDesc().SetIndex); // should be 1
	}
}