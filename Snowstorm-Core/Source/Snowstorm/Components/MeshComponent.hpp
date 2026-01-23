#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	struct MeshComponent
	{
		Ref<Mesh> MeshInstance;
		AssetHandle MeshHandle{0};
	};

	void RegisterMeshComponent();
}

