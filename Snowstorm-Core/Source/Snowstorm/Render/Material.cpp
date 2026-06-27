#include "Material.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Material::Material(const Ref<Pipeline>& pipeline)
	    : m_Pipeline(pipeline)
	{
		SS_CORE_ASSERT(m_Pipeline, "Material requires a Pipeline");

		// Default sampler (copied into MaterialInstance, can be overridden per-instance)
		SamplerDesc samp{};
		samp.MinFilter = Filter::Linear;
		samp.MagFilter = Filter::Linear;
		samp.MipmapMode = SamplerMipmapMode::Linear;
		samp.AddressU = SamplerAddressMode::Repeat;
		samp.AddressV = SamplerAddressMode::Repeat;
		samp.AddressW = SamplerAddressMode::Repeat;
		samp.EnableAnisotropy = true;
		samp.MaxAnisotropy = 16.0f;
		samp.DebugName = "MaterialDefaultSampler";

		m_DefaultSampler = Sampler::Create(samp);
		SS_CORE_ASSERT(m_DefaultSampler, "Failed to create default material sampler");
	}

	void Material::SetAlbedoTexture(const Ref<TextureView>& view)
	{
		m_AlbedoTexture = view;
		m_DefaultConstants.AlbedoTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
	}

	void Material::SetNormalTexture(const Ref<TextureView>& view)
	{
		m_NormalTexture = view;
		m_DefaultConstants.NormalTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
	}

	void Material::SetMetallicRoughnessTexture(const Ref<TextureView>& view)
	{
		m_MetallicRoughnessTexture = view;
		m_DefaultConstants.MetallicRoughnessTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
	}

	void Material::SetAOTexture(const Ref<TextureView>& view)
	{
		m_AOTexture = view;
		m_DefaultConstants.AOTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
	}

	void Material::SetEmissiveTexture(const Ref<TextureView>& view)
	{
		m_EmissiveTexture = view;
		m_DefaultConstants.EmissiveTextureIndex = view ? view->GetGlobalBindlessIndex() : 0;
	}
}
