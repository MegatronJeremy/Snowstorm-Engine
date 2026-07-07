#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <string>

namespace Snowstorm
{
	// Live console-variable editor: lists every registered CVar with a type-appropriate widget (checkbox /
	// int drag / float drag / text) so engine flags (shadows, IBL, exposure, validation, ...) can be tweaked
	// at runtime instead of only via env/CLI at startup. Also a one-line command console ("name value") for
	// setting a CVar by name. This is the runtime-mutation half the CVar system was built toward — most
	// engine CVars are already read per-frame via .Get(), so edits take effect immediately.
	//
	// Visibility is toggled from the editor's Debug menu (see EditorMenuSystem); a closed window skips all
	// ImGui work.
	class CVarPanelSystem final : public System
	{
	public:
		explicit CVarPanelSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Toggled by the Debug menu. Static so the menu system can flip it without holding a pointer to this
		// system (mirrors how the panel visibility is the only shared state).
		static bool s_Open;

	private:
		char m_Command[256] = {}; // console input buffer: "cvar.name value"
		char m_Filter[128] = {};  // name substring filter for the list
	};
}
