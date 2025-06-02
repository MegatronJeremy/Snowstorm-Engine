#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Material.hpp"

namespace Snowstorm
{
	struct MaterialComponent
	{
		Ref<Material> MaterialInstance;
	};

	void RegisterMaterialComponent();
}
