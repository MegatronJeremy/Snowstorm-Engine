#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace Snowstorm
{
	// The engine's default surface shader. A material with no explicit shader (the overwhelming common
	// case — every imported mesh material, every hand-authored PBR material) uses this. Kept as a named
	// constant, not a hardcoded literal at the pipeline site, so there is exactly one place that decides
	// "what does an unspecified material render with".
	inline constexpr const char* kDefaultFragmentShader = "Engine/Shaders/DefaultLit.frag.hlsl";

	struct MaterialAsset
	{
		// Fragment-shader path this material renders with (the vertex stage is always the shared mesh
		// vertex shader). Data-driven — Core does NOT enumerate known shaders. A custom material (e.g. the
		// Mandelbrot demo) just names its own .frag.hlsl here, so client shaders need no engine change.
		// Empty in a freshly loaded struct; MaterialAssetIO fills it (or defaults to kDefaultFragmentShader).
		std::string FragmentShader = kDefaultFragmentShader;

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

		// Alpha-cutout (glTF alphaMode MASK): discard albedo texels with alpha < AlphaCutoff. Opaque-pass
		// masking (no blending/sorting); used by foliage/fence textures. AlphaMask off = fully opaque.
		bool AlphaMask = false;
		float AlphaCutoff = 0.5f; // glTF default
	};
}
