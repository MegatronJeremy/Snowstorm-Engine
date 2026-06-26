#include "pch.h"
#include "EditorHistorySingleton.hpp"

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
	}
}
