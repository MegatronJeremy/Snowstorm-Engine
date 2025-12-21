#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

namespace Snowstorm
{
	struct MaterialComponent
	{
		Ref<MaterialInstance> MaterialInstance;
	};

	void RegisterMaterialComponent();
}
