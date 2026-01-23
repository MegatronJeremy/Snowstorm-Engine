#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/ECS/SingletonManager.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Input/InputEventBridge.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"
#include "Snowstorm/Utility/UUID.hpp"

namespace Snowstorm
{
	class SystemManager;
	class Entity;

	class World final : public NonCopyable
	{
	public:
		World();

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());

		void Clear() const;

		[[nodiscard]] SystemManager& GetSystemManager();
		[[nodiscard]] const SystemManager& GetSystemManager() const;

		[[nodiscard]] SingletonManager& GetSingletonManager();
		[[nodiscard]] const SingletonManager& GetSingletonManager() const;

		[[nodiscard]] TrackedRegistry& GetRegistry();
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

		Scope<InputEventBridge> m_InputEventBridge;

		friend class Entity;
		friend class SceneHierarchyPanel;
	};
}
