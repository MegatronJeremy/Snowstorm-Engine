#include "pch.h"
#include "World.hpp"

#include "Entity.hpp"
#include <Snowstorm/ECS/SystemManager.hpp>

#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Events/Event.h"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/Render/Renderer3DSingleton.hpp"
#include "Snowstorm/Render/Shader.hpp"

#include "Snowstorm/Systems/CameraControllerSystem.hpp"

namespace Snowstorm
{
	World::World()
	{
		m_SystemManager = CreateScope<SystemManager>(this);
		m_SingletonManager = CreateScope<SingletonManager>();

		m_SingletonManager->RegisterSingleton<EventsHandlerSingleton>();
		m_SingletonManager->RegisterSingleton<ShaderLibrarySingleton>();
		m_SingletonManager->RegisterSingleton<MeshLibrarySingleton>();
		m_SingletonManager->RegisterSingleton<Renderer3DSingleton>();
	}

	Entity World::CreateEntity(const std::string& name)
	{
		Entity entity = {m_SystemManager->GetRegistry().create(), this};

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		return entity;
	}

	TrackedRegistry& World::GetRegistry() const
	{
		return m_SystemManager->GetRegistry();
	}

	SystemManager& World::GetSystemManager() const
	{
		return *m_SystemManager;
	}

	void World::OnUpdate(const Timestep ts) const
	{
		m_SystemManager->ExecuteSystems(ts);
	}
}
