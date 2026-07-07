#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/ECS/SingletonManager.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Input/InputEventBridge.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace Snowstorm
{
	class SystemManager;
	class Entity;

	class World final : public NonCopyable
	{
	public:
		World();
		// Out-of-line: m_SystemManager is a unique_ptr to a forward-declared type, so the
		// destructor must be emitted in World.cpp where SystemManager is a complete type.
		~World();

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());

		// Find an entity by its IDComponent UUID, or return an invalid Entity if none matches. Linear
		// scan over view<IDComponent> — fine at editor entity counts. Used by undo/redo commands, which
		// must reference entities by stable UUID (entt handles are recycled across destroy/create).
		[[nodiscard]] Entity FindEntityByUUID(UUID uuid) const;

		// Queue an entity for destruction at the end of the current frame. Deferred so callers can
		// request deletion while iterating an ECS view (e.g. from a UI system) without invalidating
		// the iteration. FlushDestroyQueue() performs the actual destroy and is called once per frame.
		void DestroyEntity(Entity entity);
		void FlushDestroyQueue();

		void Clear() const;

		// "Open Scene" wipe: destroys scene entities but keeps engine-owned ones tagged
		// DoNotSerializeComponent (the editor's persistent Scene-view camera/viewport) alive across the
		// load. Use this for scene transitions; Clear() is full teardown.
		void ClearSceneEntities() const;

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

		template <typename T>
		bool HasSingleton() const
		{
			return m_SingletonManager->HasSingleton<T>();
		}

		void OnUpdate(Timestep ts);

	private:
		Scope<SystemManager> m_SystemManager;
		Scope<SingletonManager> m_SingletonManager;

		Scope<InputEventBridge> m_InputEventBridge;

		std::vector<entt::entity> m_PendingDestroy; // flushed at end of frame by FlushDestroyQueue

		friend class Entity;
		friend class SceneHierarchyPanel;
	};
}
