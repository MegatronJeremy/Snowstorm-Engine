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

		// Clamp-to-edge sampler for lookup textures that must not wrap (BRDF LUT). Same linear filtering,
		// no anisotropy (a 2D LUT doesn't need it), and ClampToEdge so a bilinear tap at the u/v extremes
		// can't wrap to the opposite edge.
		SamplerDesc clamp{};
		clamp.MinFilter = Filter::Linear;
		clamp.MagFilter = Filter::Linear;
		clamp.MipmapMode = SamplerMipmapMode::Linear;
		clamp.AddressU = SamplerAddressMode::ClampToEdge;
		clamp.AddressV = SamplerAddressMode::ClampToEdge;
		clamp.AddressW = SamplerAddressMode::ClampToEdge;
		clamp.EnableAnisotropy = false;
		clamp.DebugName = "MaterialClampSampler";

		m_ClampSampler = Sampler::Create(clamp);
		SS_CORE_ASSERT(m_ClampSampler, "Failed to create clamp material sampler");

		// Depth-comparison sampler for hardware PCF shadows (#60): EnableCompare + LessOrEqual runs the depth
		// test inside the sampler and bilinearly filters the 0/1 results, so one SampleCmp tap = a 2x2 PCF.
		// Clamp-to-edge (shadow UVs are already clamped to the atlas tile) and linear filtering (the whole
		// point — nearest would defeat the hardware filtering). Matches the manual currentDepth <= storedDepth.
		SamplerDesc shadowCmp{};
		shadowCmp.MinFilter = Filter::Linear;
		shadowCmp.MagFilter = Filter::Linear;
		shadowCmp.MipmapMode = SamplerMipmapMode::Nearest;
		shadowCmp.AddressU = SamplerAddressMode::ClampToEdge;
		shadowCmp.AddressV = SamplerAddressMode::ClampToEdge;
		shadowCmp.AddressW = SamplerAddressMode::ClampToEdge;
		shadowCmp.EnableAnisotropy = false;
		shadowCmp.EnableCompare = true;
		shadowCmp.Compare = CompareOp::LessOrEqual;
		shadowCmp.DebugName = "MaterialShadowCmpSampler";

		m_ShadowCmpSampler = Sampler::Create(shadowCmp);
		SS_CORE_ASSERT(m_ShadowCmpSampler, "Failed to create shadow comparison sampler");
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
