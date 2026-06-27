#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <string>

namespace Snowstorm
{
	// Backing state for the bottom status bar. Holds two kinds of data:
	//  - persistent context (scene file + unsaved-changes flag) that the bar shows continuously, and
	//  - a transient "last action" line (undo/redo, save, ...) that fades after a few seconds so the
	//    bar isn't permanently advertising a stale message.
	// Editor-only, never serialized.
	class EditorStatusBarSingleton final : public Singleton
	{
	public:
		// Persistent: the active scene file and whether it has unsaved edits. Pushed each frame by
		// EditorLayer (it owns the scene path); the StatusBarSystem just reads it.
		void SetScene(std::string path, const bool dirty)
		{
			m_ScenePath = std::move(path);
			m_Dirty = dirty;
		}
		[[nodiscard]] const std::string& GetScenePath() const { return m_ScenePath; }
		[[nodiscard]] bool IsDirty() const { return m_Dirty; }

		// Transient: a one-line message about the last notable action, with a countdown. The
		// StatusBarSystem ticks TimeRemaining down and only renders the text while it's positive.
		void SetMessage(std::string text, const float seconds = 4.0f)
		{
			m_Message = std::move(text);
			m_MessageTime = seconds;
		}
		[[nodiscard]] const std::string& GetMessage() const { return m_Message; }
		[[nodiscard]] float GetMessageTime() const { return m_MessageTime; }
		void TickMessage(const float dt)
		{
			if (m_MessageTime > 0.0f)
			{
				m_MessageTime -= dt;
			}
		}

	private:
		std::string m_ScenePath;
		bool m_Dirty = false;

		std::string m_Message;
		float m_MessageTime = 0.0f; // seconds remaining; <= 0 means don't show
	};
}
