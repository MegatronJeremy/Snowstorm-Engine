#pragma once

#include <functional>

namespace Snowstorm
{
	class Entity;

	class EditorCommandsSingleton : public Singleton
	{
	public:
		std::function<bool()> SaveScene;

		// Create a fresh, empty entity (Tag + ID only) and return it. Bound by the editor layer.
		std::function<Entity()> CreateEntity;

		// Queue an entity for deferred destruction. Bound by the editor layer.
		std::function<void(Entity)> DeleteEntity;

		// Replace the current scene with the procedural stress-test showcase scene (wipes the world,
		// rebuilds viewport + camera + content). Bound by the editor layer.
		std::function<void()> BuildStressScene;
	};
}