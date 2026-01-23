#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

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
		case PipelinePreset::DefaultLit: return "DefaultLit";
		case PipelinePreset::Mandelbrot: return "Mandelbrot";
		default: return "DefaultLit";
		}
	}

	inline PipelinePreset PipelinePresetFromString(const std::string& s)
	{
		if (s == "Mandelbrot") return PipelinePreset::Mandelbrot;
		return PipelinePreset::DefaultLit;
	}

	struct MaterialAsset
	{
		PipelinePreset Preset = PipelinePreset::DefaultLit;

		glm::vec4 BaseColor = glm::vec4(1.0f);

		// Optional texture references by handle
		AssetHandle AlbedoTexture{0};
	};
}
