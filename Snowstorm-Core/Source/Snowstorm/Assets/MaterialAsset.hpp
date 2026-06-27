#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace Snowstorm
{
	enum class PipelinePreset
	{
		DefaultLit = 0,
		Mandelbrot = 1 // TODO THIS SHOULD NOT BE HERE
	};

	inline std::string PipelinePresetToString(const PipelinePreset p)
	{
		switch (p)
		{
		case PipelinePreset::DefaultLit:
			return "DefaultLit";
		case PipelinePreset::Mandelbrot:
			return "Mandelbrot";
		default:
			return "DefaultLit";
		}
	}

	inline PipelinePreset PipelinePresetFromString(const std::string& s)
	{
		if (s == "Mandelbrot")
			return PipelinePreset::Mandelbrot;
		return PipelinePreset::DefaultLit;
	}

	struct MaterialAsset
	{
		PipelinePreset Preset = PipelinePreset::DefaultLit;

		glm::vec4 BaseColor = glm::vec4(1.0f);

		// PBR texture references by handle (0 = absent -> shader falls back to scalar/default).
		// Albedo + emissive are sRGB source data; normal / metallic-roughness / AO are linear (see the
		// importer and GetTextureView's srgb flag).
		AssetHandle AlbedoTexture{0};
		AssetHandle NormalTexture{0};
		AssetHandle MetallicRoughnessTexture{0}; // glTF packing: G = roughness, B = metallic
		AssetHandle AOTexture{0};
		AssetHandle EmissiveTexture{0};

		// Scalar PBR factors (multiplied with the corresponding texture, or used alone when absent).
		float Metallic = 0.0f;
		float Roughness = 1.0f;
		glm::vec3 EmissiveColor = glm::vec3(0.0f);
	};
}
