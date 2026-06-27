#include "pch.h"
#include "EditorHistorySingleton.hpp"

#include "Snowstorm/World/EditorCommands.hpp"

namespace Snowstorm
{
	void EditorHistorySingleton::Push(const Ref<EditorCommand>& command)
	{
		if (!command)
		{
			return;
		}

		m_Redo.clear(); // a new action invalidates the redo branch
		m_Undo.push_back(command);

		// Bound memory: drop the oldest entries past the cap.
		if (m_Undo.size() > kMaxDepth)
		{
			m_Undo.erase(m_Undo.begin(), m_Undo.begin() + static_cast<long>(m_Undo.size() - kMaxDepth));
		}
	}

	void EditorHistorySingleton::Undo(World& world)
	{
		if (m_Undo.empty())
		{
			return;
		}

		const Ref<EditorCommand> cmd = m_Undo.back();
		m_Undo.pop_back();
		cmd->Undo(world);
		m_Redo.push_back(cmd);
	}

	void EditorHistorySingleton::Redo(World& world)
	{
		if (m_Redo.empty())
		{
			return;
		}

		const Ref<EditorCommand> cmd = m_Redo.back();
		m_Redo.pop_back();
		cmd->Redo(world);
		m_Undo.push_back(cmd);
	}

	void EditorHistorySingleton::Clear()
	{
		m_Undo.clear();
		m_Redo.clear();
		m_PendingEdit = {};
		m_CleanIndex = 0; // a freshly loaded/empty scene is clean
	}

	void EditorHistorySingleton::BeginEdit(const UUID target, const std::string& typeName, nlohmann::json before)
	{
		m_PendingEdit.Active = true;
		m_PendingEdit.Target = target;
		m_PendingEdit.TypeName = typeName;
		m_PendingEdit.Before = std::move(before);
	}

	void EditorHistorySingleton::FinalizeEdit(nlohmann::json after)
	{
		if (!m_PendingEdit.Active)
		{
			return;
		}

		// Only record if something actually changed across the interaction.
		if (m_PendingEdit.Before != after)
		{
			Push(CreateRef<ComponentEditCommand>(m_PendingEdit.Target, m_PendingEdit.TypeName,
			                                     std::move(m_PendingEdit.Before), std::move(after)));
		}
		m_PendingEdit = {};
	}
}
