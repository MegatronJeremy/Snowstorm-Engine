#pragma once

#include <unordered_map>
#include "Mesh.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"

namespace Snowstorm
{
	class MeshLibrarySingleton final : public Singleton
	{
	public:
		// Load a whole model file as a single flattened mesh (all submeshes merged, one material).
		Ref<Mesh> Load(const std::string& filepath);

		// Load a single submesh (aiMesh) of a model file as its own mesh. Vertices are pre-transformed
		// by their node hierarchy (aiProcess_PreTransformVertices) so each part sits in model space;
		// this is what model import uses to spawn one entity per part. submeshIndex must be in range.
		Ref<Mesh> Load(const std::string& filepath, int submeshIndex);

		void Clear();
		bool Remove(const std::string& filepath);

	private:
		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
	};
}
