#include "pch.h"
#include "EditorCommands.hpp"

#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

namespace Snowstorm
{
	// ---- TransformCommand ----------------------------------------------------------------------------

	void TransformCommand::Undo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (e && e.HasComponent<TransformComponent>())
		{
			e.PatchComponent<TransformComponent>([&](TransformComponent& t)
			                                     { t = m_Before; });
		}
	}

	void TransformCommand::Redo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (e && e.HasComponent<TransformComponent>())
		{
			e.PatchComponent<TransformComponent>([&](TransformComponent& t)
			                                     { t = m_After; });
		}
	}

	// ---- AddEntityCommand (create / duplicate) -------------------------------------------------------

	void AddEntityCommand::Undo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (!e)
		{
			return;
		}

		// Snapshot the full entity so Redo can bring it back exactly as it is now.
		SceneSerializer::SerializeEntity(e, m_Snapshot);
		world.DestroyEntity(e);
	}

	void AddEntityCommand::Redo(World& world)
	{
		if (m_Snapshot.is_null())
		{
			return; // first Redo after a normal Push never happens (the create already applied)
		}
		SceneSerializer::DeserializeEntity(world, m_Snapshot);
	}

	// ---- DeleteEntityCommand -------------------------------------------------------------------------

	void DeleteEntityCommand::Undo(World& world)
	{
		if (!m_Snapshot.is_null())
		{
			SceneSerializer::DeserializeEntity(world, m_Snapshot);
		}
	}

	void DeleteEntityCommand::Redo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (e)
		{
			world.DestroyEntity(e);
		}
	}

	// ---- RenameCommand -------------------------------------------------------------------------------

	void RenameCommand::Undo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (e && e.HasComponent<TagComponent>())
		{
			e.WriteComponent<TagComponent>().Tag = m_Before;
		}
	}

	void RenameCommand::Redo(World& world)
	{
		Entity e = world.FindEntityByUUID(m_Target);
		if (e && e.HasComponent<TagComponent>())
		{
			e.WriteComponent<TagComponent>().Tag = m_After;
		}
	}
}
