#pragma once
#include <cstdint>
#include <glm/vec4.hpp>
#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	enum class MaterialOverrideMask : uint8_t
	{
		None      = 0,
		BaseColor = 1u << 0,
		AlbedoTex = 1u << 1,
	};

	inline bool HasOverride(uint32_t mask, MaterialOverrideMask bit)
	{
		return (mask & static_cast<uint32_t>(bit)) != 0;
	}

	inline void SetOverride(uint32_t& mask, MaterialOverrideMask bit, bool enabled)
	{
		if (enabled) mask |= static_cast<uint32_t>(bit);
		else         mask &= ~static_cast<uint32_t>(bit);
	}

	struct MaterialOverridesComponent
	{
		uint32_t OverrideMask = static_cast<uint32_t>(MaterialOverrideMask::None); // TODO I REALLY DON'T LIKE THIS APPROACH

		glm::vec4 BaseColorOverride{1.0f};
		AssetHandle AlbedoTextureOverride{0};
		// later...
	};

	inline bool operator==(const MaterialOverridesComponent& lhs, const MaterialOverridesComponent& rhs)
	{
		return lhs.OverrideMask == rhs.OverrideMask && lhs.BaseColorOverride == rhs.BaseColorOverride && lhs.AlbedoTextureOverride == rhs.AlbedoTextureOverride;
	}

	void RegisterMaterialOverridesComponent();
}
