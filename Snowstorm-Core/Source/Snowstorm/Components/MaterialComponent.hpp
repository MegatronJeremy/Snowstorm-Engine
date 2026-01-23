#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

namespace Snowstorm
{
	struct MaterialComponent
	{
		Ref<MaterialInstance> MaterialInstance;
		AssetHandle Material{0};
	};

	void RegisterMaterialComponent();
}
