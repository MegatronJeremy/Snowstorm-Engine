#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorNotificationsSingleton;

	class EditorMenuSystem final : public System
	{
	public:
		explicit EditorMenuSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		void DrawImportModelPopup(EditorNotificationsSingleton& notify);

		// Keyboard & mouse shortcut reference window (Help menu). Keep its contents in sync with the
		// actual bindings whenever a shortcut is added or changed — see CLAUDE.md.
		void DrawShortcutsWindow();

		bool m_ShowImportPopup = false;
		bool m_ShowShortcuts = false;
		char m_ImportPathBuffer[512] = {};
	};
}
