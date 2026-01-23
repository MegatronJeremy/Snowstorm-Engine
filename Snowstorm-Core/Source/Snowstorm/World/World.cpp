#include "pch.h"
#include "World.hpp"

#include "Entity.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Events/Event.hpp"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"

namespace Snowstorm
{
	World::World()
	{
		m_SystemManager = CreateScope<SystemManager>(this);
		m_SingletonManager = CreateScope<SingletonManager>();

		m_SingletonManager->RegisterSingleton<InputStateSingleton>();

		m_SingletonManager->RegisterSingleton<ShaderLibrarySingleton>();
		m_SingletonManager->RegisterSingleton<MeshLibrarySingleton>();
		m_SingletonManager->RegisterSingleton<EditorCommandsSingleton>();
		m_SingletonManager->RegisterSingleton<RendererSingleton>();

		m_SingletonManager->RegisterSingleton<AssetManagerSingleton>(this);

		// Create the bridge to the Application event bus
		auto& bus = Application::Get().GetEventBus();
		auto& input = m_SingletonManager->GetSingleton<InputStateSingleton>();
		m_InputEventBridge = CreateScope<InputEventBridge>(bus, input);
	}

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

	void World::Clear() const
	{
		m_SystemManager->GetRegistry().Clear();
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

	void World::OnUpdate(const Timestep ts) const
	{
		m_SystemManager->ExecuteSystems(ts);
		m_SingletonManager->GetSingleton<InputStateSingleton>().Clear();
	}
}
