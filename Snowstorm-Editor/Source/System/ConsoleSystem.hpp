#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// In-editor developer console: a scrolling log view (the engine's log stream, captured via LogBuffer)
	// with a command input at the bottom — the Unreal Output Log / Quake-console model, where output and
	// commands share one surface so a command's result prints right above where you typed it.
	//
	// Commands: "<cvar.name> <value>" sets a console variable; plus built-ins help / clear / list [filter].
	// Command results and errors are logged (SS_INFO/SS_WARN), so they appear in the same stream. The
	// browsable/visual CVar editor stays in CVarPanelSystem — this is the text-command half.
	//
	// Toggled from the editor's Debug menu; a closed window does no work.
	class ConsoleSystem final : public System
	{
	public:
		explicit ConsoleSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		static bool s_Open;

	private:
		void ExecuteCommand(const char* line);

		char m_Input[256] = {};
		int m_LevelFilter = 0; // minimum level shown (index into the level names); 0 = Trace (all)
		bool m_AutoScroll = true;
		uint64_t m_LastRevision = 0; // last LogBuffer revision we scrolled for (autoscroll trigger)
	};
}
