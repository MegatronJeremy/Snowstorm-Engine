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
		Ref<Mesh> Load(const std::string& filepath);
		Ref<Mesh> CreateQuad();

		void Clear();
		bool Remove(const std::string& filepath);

	private:
		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
	};
}
