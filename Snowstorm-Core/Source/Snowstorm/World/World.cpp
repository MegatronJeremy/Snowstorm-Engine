#include "World.hpp"

#include "Entity.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Events/Event.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"

namespace Snowstorm
{
	World::World()
	{
		m_SystemManager = CreateScope<SystemManager>(this);
		m_SingletonManager = CreateScope<SingletonManager>();

		// World-scoped, per-scene state only. The renderer + shader/mesh libraries are device-lifetime and
		// now live in the application's ServiceManager (see RegisterCoreServices), shared across all Worlds.
		m_SingletonManager->RegisterSingleton<InputStateSingleton>();
		m_SingletonManager->RegisterSingleton<EditorCommandsSingleton>();

		m_SingletonManager->RegisterSingleton<AssetManagerSingleton>(this);

		// Create the bridge to the Application event bus. In a headless context (unit tests) there is no
		// Application, so skip the bridge — input simply never fires, which is fine for logic-only tests.
		if (Application::Exists())
		{
			auto& bus = Application::Get().GetEventBus();
			auto& input = m_SingletonManager->GetSingleton<InputStateSingleton>();
			m_InputEventBridge = CreateScope<InputEventBridge>(bus, input);
		}
	}

	World::~World() = default;

	Entity World::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID{}, name);
	}

	Entity World::CreateEntityWithUUID(const UUID uuid, const std::string& name)
	{
		Entity entity = {m_SystemManager->GetRegistry().create(), this};

		auto& id = entity.AddComponent<IDComponent>();
		id.Id = uuid;

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		return entity;
	}

	Entity World::FindEntityByUUID(const UUID uuid) const
	{
		auto& reg = m_SystemManager->GetRegistry();
		for (const auto view = reg.view<IDComponent>(); const entt::entity e : view)
		{
			if (reg.Read<IDComponent>(e).Id == uuid)
			{
				return Entity{e, const_cast<World*>(this)};
			}
		}
		return Entity{entt::null, const_cast<World*>(this)};
	}

	void World::DestroyEntity(const Entity entity)
	{
		if (!entity)
		{
			return;
		}
		m_PendingDestroy.push_back(entity.Handle());
	}

	void World::FlushDestroyQueue()
	{
		if (m_PendingDestroy.empty())
		{
			return;
		}

		auto& reg = m_SystemManager->GetRegistry();
		for (const entt::entity e : m_PendingDestroy)
		{
			if (reg.valid(e))
			{
				reg.destroy(e);
			}
		}
		m_PendingDestroy.clear();
	}

	void World::Clear() const
	{
		m_SystemManager->GetRegistry().Clear();
	}

	void World::ClearSceneEntities() const
	{
		m_SystemManager->GetRegistry().ClearExcept<DoNotSerializeComponent>();

		// Advance the scene generation so temporal-history consumers (RenderSystem's TAA / neural upscaler)
		// can detect the wipe and drop their stale per-viewport history — the persistent viewport survives
		// this clear, so without the signal its TAA/neural history would reproject the OLD scene for one
		// frame. See World::SceneGeneration().
		++m_SceneGeneration;

		// Clearing entities leaves the editor's selected-entity handle dangling. Its consumers all
		// guard with IsValid(), so this isn't the crash it looks like — but a selection pointing at a
		// destroyed entity is still wrong state (a stale inspector target), so reset it on every wipe.
		// Guarded: runtime worlds never register the singleton. (The New-Scene crash itself was a stale
		// VisibilityCache handle in the render pass, fixed there.)
		if (HasSingleton<EditorSelectionSingleton>())
		{
			auto& selection = GetSingleton<EditorSelectionSingleton>();
			selection.Selected = {};
			selection.GizmoActive = false;
		}
	}

	SystemManager& World::GetSystemManager()
	{
		return *m_SystemManager;
	}

	const SystemManager& World::GetSystemManager() const
	{
		return *m_SystemManager;
	}

	SingletonManager& World::GetSingletonManager()
	{
		return *m_SingletonManager;
	}

	const SingletonManager& World::GetSingletonManager() const
	{
		return *m_SingletonManager;
	}

	TrackedRegistry& World::GetRegistry()
	{
		return m_SystemManager->GetRegistry();
	}

	TrackedRegistry& World::GetRegistry() const
	{
		return m_SystemManager->GetRegistry();
	}

	void World::OnUpdate(const Timestep ts)
	{
		m_SystemManager->ExecuteSystems(ts);
		FlushDestroyQueue(); // apply deferred entity deletions after all systems have run
		m_SingletonManager->GetSingleton<InputStateSingleton>().Clear();
	}
}
