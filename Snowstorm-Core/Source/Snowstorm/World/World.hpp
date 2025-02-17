#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/ECS/SingletonManager.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"

namespace Snowstorm
{
	class SystemManager;
	class Entity;

	class World final : public NonCopyable
	{
	public:
		World();

		Entity CreateEntity(const std::string& name = std::string());

		[[nodiscard]] SystemManager& GetSystemManager() const;

		[[nodiscard]] TrackedRegistry& GetRegistry() const;

		template <typename T>
		T& GetSingleton() const
		{
			return m_SingletonManager->GetSingleton<T>();
		}

		void OnUpdate(Timestep ts) const;

	private:
		Scope<SystemManager> m_SystemManager;
		Scope<SingletonManager> m_SingletonManager;

		friend class Entity;
		friend class SceneHierarchyPanel;
	};
}
