#include "pch.h"
#include "Entity.hpp"

namespace Snowstorm
{
	Entity::Entity(const entt::entity handle, World* world)
		: m_EntityHandle(handle), m_World(std::move(world))
	{
	}
}
