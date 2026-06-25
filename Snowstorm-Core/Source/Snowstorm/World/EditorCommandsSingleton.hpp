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
	};
}