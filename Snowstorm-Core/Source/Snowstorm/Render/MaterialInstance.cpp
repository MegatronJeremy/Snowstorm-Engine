#include "MaterialInstance.hpp"

#include <entt/entity/entity.hpp>

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMaterialConstantsBinding = 0;
		constexpr uint32_t kMaterialSamplerBinding = 1;      // LinearSampler (wrapping, per-instance)
		constexpr uint32_t kMaterialClampSamplerBinding = 2; // ClampSampler (engine-global, for LUTs)
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

		MarkDirty();
	}

	const Ref<Pipeline>& MaterialInstance::GetPipeline() const
	{
		return m_Base->GetPipeline();
	}

	void MaterialInstance::MarkDirty()
	{
		// Every frame-in-flight's copy needs re-upload.
		m_FrameDirty.assign(Renderer::GetFramesInFlight(), true);
	}

	void MaterialInstance::SetAlbedoTexture(const Ref<TextureView>& view)
	{
		m_AlbedoTexture = view;
		m_Constants.AlbedoTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		MarkDirty();
	}

	void MaterialInstance::SetNormalTexture(const Ref<TextureView>& view)
	{
		m_Constants.NormalTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		MarkDirty();
	}

	void MaterialInstance::SetMetallicRoughnessTexture(const Ref<TextureView>& view)
	{
		m_Constants.MetallicRoughnessTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		MarkDirty();
	}

	void MaterialInstance::SetAOTexture(const Ref<TextureView>& view)
	{
		m_Constants.AOTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		MarkDirty();
	}

	void MaterialInstance::SetEmissiveTexture(const Ref<TextureView>& view)
	{
		m_Constants.EmissiveTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
		MarkDirty();
	}

	void MaterialInstance::SetBaseColor(const glm::vec4& color)
	{
		m_Constants.BaseColor = color;
		MarkDirty();
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
			m_MaterialDataSets[frameIndex]->SetSampler(kMaterialClampSamplerBinding, m_Base->GetClampSampler());
		}
	}

	void MaterialInstance::UpdateGPU(const uint32_t frameIndex)
	{
		EnsurePerFrameResources(frameIndex);

		if (m_FrameDirty.size() <= frameIndex)
		{
			m_FrameDirty.resize(frameIndex + 1, true);
		}

		// Commit this frame's set at most once. A shared instance is Apply()'d by many batches per
		// frame; re-committing after it's bound would invalidate the recording command buffer.
		if (m_FrameDirty[frameIndex])
		{
			m_UniformBuffers[frameIndex]->SetData(&m_Constants, sizeof(Material::Constants), 0);

			const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(Material::Constants)};
			m_MaterialDataSets[frameIndex]->SetBuffer(kMaterialConstantsBinding, bb);
			m_MaterialDataSets[frameIndex]->SetSampler(kMaterialSamplerBinding, m_Sampler);
			m_MaterialDataSets[frameIndex]->SetSampler(kMaterialClampSamplerBinding, m_Base->GetClampSampler());

			m_MaterialDataSets[frameIndex]->Commit();

			m_FrameDirty[frameIndex] = false;
		}
	}

	void MaterialInstance::Apply(CommandContext& ctx, const uint32_t frameIndex)
	{
		UpdateGPU(frameIndex);

		// Bind the pipeline only. Set 1 (this material's data) is no longer bound here: the caller binds
		// it together with sets 0 and 2 in a single contiguous BindDescriptorSets right before the draw
		// (see RendererService::FlushBatch), so all per-draw sets land in one call. GetDescriptorSet
		// exposes the set-1 handle for that batched bind.
		ctx.BindPipeline(m_Base->GetPipeline());
	}
}