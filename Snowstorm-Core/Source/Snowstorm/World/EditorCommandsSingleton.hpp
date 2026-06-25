#pragma once

#include <functional>
#include <string>

namespace Snowstorm
{
	class Entity;

	class EditorCommandsSingleton : public Singleton
	{
	public:
		std::function<bool()> SaveScene;

		// Open (load) a scene from a .world file path, replacing the current scene. Returns success.
		// Bound by the editor layer.
		std::function<bool(const std::string&)> OpenScene;

		// Create a fresh, empty entity (Tag + ID only) and return it. Bound by the editor layer.
		std::function<Entity()> CreateEntity;

		// Queue an entity for deferred destruction. Bound by the editor layer.
		std::function<void(Entity)> DeleteEntity;
	};
}