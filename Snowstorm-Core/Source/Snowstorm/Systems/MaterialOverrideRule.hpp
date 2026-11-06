#pragma once

#include "Snowstorm/Components/MaterialOverridesComponent.hpp"

namespace Snowstorm
{
	// AlbedoTexture rides the per-instance buffer (RenderSystem resolves it to a bindless index), so it
	// does not force a unique MaterialInstance: objects with different albedo textures still batch.
	inline bool IsPerInstanceOverride(const MaterialOverride& o)
	{
		return o.Type == MaterialOverrideType::Texture && o.Name == "AlbedoTexture";
	}

	// Any override that is not per-instance needs a baked unique instance, whether or not
	// ApplyBakedOverrides recognises its name.
	inline bool NeedsUniqueInstance(const MaterialOverridesComponent& ov)
	{
		for (const MaterialOverride& o : ov.Overrides)
		{
			if (!IsPerInstanceOverride(o))
			{
				return true;
			}
		}
		return false;
	}

	// A known name with the wrong type is ignored, so the instance keeps the asset's value.
	template <typename Instance>
	void ApplyBakedOverrides(const MaterialOverridesComponent& ov, Instance& mi)
	{
		for (const MaterialOverride& o : ov.Overrides)
		{
			if (IsPerInstanceOverride(o))
			{
				continue;
			}
			if (o.Name == "BaseColor" && o.Type == MaterialOverrideType::Color)
			{
				mi.SetBaseColor(o.Color);
			}
			else if (o.Name == "Roughness" && o.Type == MaterialOverrideType::Float)
			{
				mi.SetRoughness(o.Scalar);
			}
			else if (o.Name == "Metallic" && o.Type == MaterialOverrideType::Float)
			{
				mi.SetMetallic(o.Scalar);
			}
		}
	}
}
