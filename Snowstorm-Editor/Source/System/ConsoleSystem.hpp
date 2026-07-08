#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

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

		// Recompute m_Candidates from the current input's first token (command/CVar-name prefix). Called
		// whenever the input text changes.
		void RefreshCandidates();

		// ImGui InputText callback: handles Tab completion (complete to the shared prefix, or accept the
		// highlighted suggestion) and keeps m_Candidates fresh on edits. Static thunk -> member.
		static int InputCallback(ImGuiInputTextCallbackData* data);
		int OnInputCallback(ImGuiInputTextCallbackData* data);

		char m_Input[256] = {};
		int m_LevelFilter = 0; // minimum level shown (index into the level names); 0 = Trace (all)
		bool m_AutoScroll = true;
		uint64_t m_LastRevision = 0; // last LogBuffer revision we scrolled for (autoscroll trigger)

		// Autocomplete state: names matching the current prefix, shown in a popup and completed with Tab
		// (repeated Tab cycles). Rebuilt on every edit via the input callback.
		std::vector<std::string> m_Candidates;
		int m_SelectedCandidate = -1; // index into m_Candidates; -1 = none highlighted

		// Command history (like a shell / Quake console): submitted commands, newest last. Up/Down in the
		// input walk it. m_HistoryPos == size() means "current (unsubmitted) line"; walking up decrements.
		std::vector<std::string> m_History;
		int m_HistoryPos = 0; // == m_History.size() when not browsing
	};
}
