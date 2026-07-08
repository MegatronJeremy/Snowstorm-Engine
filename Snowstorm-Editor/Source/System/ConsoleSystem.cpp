#include "ConsoleSystem.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Core/LogBuffer.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <imgui.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Open by default: the console is part of the persistent bottom dock (see DockspaceSetupSystem), so it
	// shows the log stream on launch like a pro editor's Output Log rather than being a hidden toggle.
	bool ConsoleSystem::s_Open = true;

	namespace
	{
		constexpr std::array<const char*, 6> kLevelNames = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};

		// Built-in console commands, offered by autocomplete alongside CVar names.
		constexpr std::array<const char*, 3> kBuiltins = {"help", "clear", "list"};

		ImVec4 LevelColor(const LogBuffer::Level level)
		{
			switch (level)
			{
			case LogBuffer::Level::Trace:
				return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			case LogBuffer::Level::Debug:
				return ImVec4(0.55f, 0.70f, 0.90f, 1.0f);
			case LogBuffer::Level::Info:
				return ImVec4(0.86f, 0.87f, 0.88f, 1.0f);
			case LogBuffer::Level::Warn:
				return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
			case LogBuffer::Level::Error:
				return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);
			case LogBuffer::Level::Critical:
				return ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
			}
			return ImVec4(1, 1, 1, 1);
		}

		constexpr ImVec4 kDimColor{0.50f, 0.50f, 0.52f, 1.0f};     // timestamp
		constexpr ImVec4 kCoreLogColor{0.55f, 0.80f, 0.65f, 1.0f}; // SNOWSTORM (engine) logger
		constexpr ImVec4 kAppLogColor{0.70f, 0.65f, 0.90f, 1.0f};  // APP (client) logger

		// Fixed-width 3-char level tag (Unity/Godot style). Always the same width, so columns align tightly
		// with no dead space — unlike a variable "[info]"/"[warning]" badge padded to the widest.
		const char* LevelTag(const LogBuffer::Level level)
		{
			switch (level)
			{
			case LogBuffer::Level::Trace:
				return "TRC";
			case LogBuffer::Level::Debug:
				return "DBG";
			case LogBuffer::Level::Info:
				return "INF";
			case LogBuffer::Level::Warn:
				return "WRN";
			case LogBuffer::Level::Error:
				return "ERR";
			case LogBuffer::Level::Critical:
				return "CRT";
			}
			return "???";
		}

		// Draw a colored text run at a fixed X (pixels from the line's left edge). Fixed columns keep the
		// tag / logger / message aligned across every line regardless of content.
		void ColumnText(const float x, const ImVec4& color, const char* begin, const char* end)
		{
			if (begin >= end)
			{
				return;
			}
			ImGui::SameLine(x);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted(begin, end);
			ImGui::PopStyleColor();
		}

		// Render one captured line with rich, structured coloring. The sink now writes "[HH:MM:SS] LOGGER:
		// message" (the level is NOT in the text — it comes from Entry::LevelValue). Layout: dim timestamp,
		// a fixed-width colored level tag, logger-name color (engine vs client), message in the level color.
		// Fixed character columns (monospace font) keep everything aligned. Falls back to a plain
		// level-colored line if the shape doesn't match (e.g. a raw multi-line message).
		void DrawRichLine(const LogBuffer::Entry& e)
		{
			const ImVec4 lvl = LevelColor(e.LevelValue);
			const std::string& s = e.Text;

			// Expect: "[time] NAME: msg". Find the timestamp bracket and the "NAME:" boundary.
			const size_t t0 = s.find('[');
			const size_t t1 = (t0 == std::string::npos) ? std::string::npos : s.find(']', t0);
			const size_t colon = (t1 == std::string::npos) ? std::string::npos : s.find(':', t1);

			if (t0 == std::string::npos || t1 == std::string::npos || colon == std::string::npos)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, lvl);
				ImGui::TextUnformatted(s.c_str());
				ImGui::PopStyleColor();
				return;
			}

			const char* base = s.c_str();
			const char* loggerBegin = base + t1 + 1; // after "] "
			while (loggerBegin < base + colon && (*loggerBegin == ' '))
			{
				++loggerBegin;
			}
			const std::string loggerName(loggerBegin, base + colon);
			const ImVec4 loggerColor = (loggerName == "APP") ? kAppLogColor : kCoreLogColor;

			// Fixed column offsets in character widths (monospace). Timestamp "[HH:MM:SS]" = 10 chars;
			// then the 3-char level tag; logger starts after both plus single-space gaps.
			const float charW = ImGui::CalcTextSize("0").x;
			const float xTag = charW * 11.0f;    // 10 (time) + 1 space
			const float xLogger = charW * 15.0f; // + 3 (tag) + 1 space
			const float lineStartX = ImGui::GetCursorPosX();

			// Timestamp (establishes the row; no SameLine).
			ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
			ImGui::TextUnformatted(base + t0, base + t1 + 1);
			ImGui::PopStyleColor();

			const char* tag = LevelTag(e.LevelValue);
			ColumnText(lineStartX + xTag, lvl, tag, tag + std::strlen(tag));              // level tag
			ColumnText(lineStartX + xLogger, loggerColor, loggerBegin, base + colon + 1); // LOGGER:

			// Message follows the logger with a one-char gap; skip the source's own leading space(s).
			const char* msgBegin = base + colon + 1;
			while (msgBegin < base + s.size() && *msgBegin == ' ')
			{
				++msgBegin;
			}
			ImGui::SameLine(0.0f, charW);
			ImGui::PushStyleColor(ImGuiCol_Text, lvl);
			ImGui::TextUnformatted(msgBegin, base + s.size());
			ImGui::PopStyleColor();
		}
	} // namespace

	void ConsoleSystem::RefreshCandidates()
	{
		m_Candidates.clear();
		m_SelectedCandidate = -1;

		// Autocomplete only the FIRST token (the command / CVar name). Once there's a space, the user is
		// typing a value, not a name — nothing to complete.
		std::string text = m_Input;
		if (text.find(' ') != std::string::npos)
		{
			return;
		}
		if (text.empty())
		{
			return;
		}

		for (const char* b : kBuiltins)
		{
			if (std::string(b).rfind(text, 0) == 0)
			{
				m_Candidates.emplace_back(b);
			}
		}
		for (const ICVar* cvar : CVarRegistry::Get().All())
		{
			if (cvar->GetName().rfind(text, 0) == 0)
			{
				m_Candidates.push_back(cvar->GetName());
			}
		}
		if (!m_Candidates.empty())
		{
			m_SelectedCandidate = 0;
		}
	}

	int ConsoleSystem::InputCallback(ImGuiInputTextCallbackData* data)
	{
		return static_cast<ConsoleSystem*>(data->UserData)->OnInputCallback(data);
	}

	int ConsoleSystem::OnInputCallback(ImGuiInputTextCallbackData* data)
	{
		switch (data->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackEdit:
			// Text changed — recompute suggestions. m_Input mirrors the buffer ImGui just edited.
			std::snprintf(m_Input, sizeof(m_Input), "%s", data->Buf);
			RefreshCandidates();
			break;

		case ImGuiInputTextFlags_CallbackCompletion:
		{
			// Tab: fill the buffer with the current candidate, then advance the selection so repeated Tab
			// CYCLES through matches (shell/devtools behavior). The buffer shows the highlighted candidate
			// (no trailing space yet, so you can keep Tabbing); the popup tracks m_SelectedCandidate.
			if (!m_Candidates.empty())
			{
				if (m_SelectedCandidate < 0)
				{
					m_SelectedCandidate = 0;
				}
				const std::string& pick = m_Candidates[static_cast<size_t>(m_SelectedCandidate)];
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, pick.c_str());
				std::snprintf(m_Input, sizeof(m_Input), "%s", pick.c_str());
				// Advance for the next Tab, WITHOUT triggering a candidate refresh (the edit callback would
				// otherwise reset the list to just-this-name). We keep m_Candidates as-is here.
				m_SelectedCandidate = (m_SelectedCandidate + 1) % static_cast<int>(m_Candidates.size());
			}
			break;
		}

		case ImGuiInputTextFlags_CallbackHistory:
			// Up/Down walk the command history (like a shell). m_HistoryPos == size() is the live line.
			if (!m_History.empty())
			{
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (m_HistoryPos > 0)
					{
						--m_HistoryPos;
					}
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (m_HistoryPos < static_cast<int>(m_History.size()))
					{
						++m_HistoryPos;
					}
				}

				const std::string line =
				    (m_HistoryPos >= 0 && m_HistoryPos < static_cast<int>(m_History.size()))
				        ? m_History[static_cast<size_t>(m_HistoryPos)]
				        : std::string{}; // past the newest entry -> empty (back to a fresh line)

				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, line.c_str());
				std::snprintf(m_Input, sizeof(m_Input), "%s", line.c_str());
				m_Candidates.clear(); // browsing history isn't prefix-completion
				m_SelectedCandidate = -1;
			}
			break;

		default:
			break;
		}
		return 0;
	}

	void ConsoleSystem::ExecuteCommand(const char* line)
	{
		std::string cmd = line;
		// Trim leading/trailing whitespace.
		const auto notSpace = [](const char c)
		{ return c != ' ' && c != '\t'; };
		while (!cmd.empty() && !notSpace(cmd.front()))
			cmd.erase(cmd.begin());
		while (!cmd.empty() && !notSpace(cmd.back()))
			cmd.pop_back();
		if (cmd.empty())
		{
			return;
		}

		// Echo the command into the log so it appears in the stream above the input (console feedback loop).
		SS_INFO("] {0}", cmd);

		const size_t sep = cmd.find(' ');
		const std::string verb = cmd.substr(0, sep);
		const std::string rest = (sep == std::string::npos) ? "" : cmd.substr(sep + 1);

		// --- Built-ins ---
		if (verb == "help")
		{
			SS_INFO("Commands: help | clear | list [filter] | <cvar.name> <value>");
			return;
		}
		if (verb == "clear")
		{
			LogBuffer::Get().Clear();
			return;
		}
		if (verb == "list")
		{
			for (const ICVar* cvar : CVarRegistry::Get().All())
			{
				if (rest.empty() || cvar->GetName().find(rest) != std::string::npos)
				{
					SS_INFO("  {0} = {1} [{2}]", cvar->GetName(), cvar->GetValueString(), cvar->GetTypeName());
				}
			}
			return;
		}

		// --- Otherwise: "<cvar.name> <value>" sets a console variable. ---
		if (rest.empty())
		{
			// Bare name -> print its current value (or unknown).
			if (const ICVar* cvar = CVarRegistry::Get().Find(verb))
			{
				SS_INFO("{0} = {1} [{2}]", cvar->GetName(), cvar->GetValueString(), cvar->GetTypeName());
			}
			else
			{
				SS_WARN("Unknown command or CVar: '{0}' (try 'help')", verb);
			}
			return;
		}

		if (ICVar* cvar = CVarRegistry::Get().Find(verb))
		{
			cvar->SetFromString(rest);
			SS_INFO("{0} = {1}", cvar->GetName(), cvar->GetValueString());
		}
		else
		{
			SS_WARN("Unknown CVar: '{0}' (try 'list')", verb);
		}
	}

	void ConsoleSystem::Execute(Timestep)
	{
		if (!s_Open)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(720.0f, 400.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Console", &s_Open))
		{
			ImGui::End();
			return;
		}

		// --- Toolbar: level filter, autoscroll, clear. ---
		ImGui::SetNextItemWidth(120.0f);
		ImGui::Combo("##level", &m_LevelFilter, kLevelNames.data(), static_cast<int>(kLevelNames.size()));
		ImGui::SameLine();
		ImGui::Checkbox("Autoscroll", &m_AutoScroll);
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			LogBuffer::Get().Clear();
		}
		ImGui::Separator();

		// --- Log view. Reserve space for the input row below (one line + separator + padding). ---
		const float inputHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
		if (ImGui::BeginChild("##log", ImVec2(0, -inputHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
		{
			const std::vector<LogBuffer::Entry> entries = LogBuffer::Get().Snapshot();
			for (const LogBuffer::Entry& e : entries)
			{
				if (static_cast<int>(e.LevelValue) < m_LevelFilter)
				{
					continue; // below the minimum level filter
				}
				DrawRichLine(e);
			}

			// Autoscroll to the bottom when new lines arrived.
			const uint64_t rev = LogBuffer::Get().Revision();
			if (m_AutoScroll && rev != m_LastRevision)
			{
				ImGui::SetScrollHereY(1.0f);
				m_LastRevision = rev;
			}
		}
		ImGui::EndChild();

		// --- Command input with autocomplete. Tab completes, Up/Down cycle suggestions, Enter submits. ---
		constexpr ImGuiInputTextFlags inputFlags =
		    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
		    ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit;

		ImGui::SetNextItemWidth(-1.0f);
		const bool submitted = ImGui::InputTextWithHint(
		    "##cmd", "Command (Tab to complete; help, clear, list, or cvar.name value)...",
		    m_Input, sizeof(m_Input), inputFlags, &ConsoleSystem::InputCallback, this);

		// Remember the input row's rectangle so the suggestions popup can be anchored under it.
		const ImVec2 inputMin = ImGui::GetItemRectMin();
		const ImVec2 inputMax = ImGui::GetItemRectMax();
		const bool inputActive = ImGui::IsItemActive() || ImGui::IsItemFocused();

		// Escape while the field is focused: fully release focus. ImGui reverts the text on Escape but keeps
		// the item nav-focused, so its highlighted/active border lingers — clearing window focus drops the
		// highlight and closes the suggestions popup. Also reset the input + autocomplete/history browse.
		if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			m_Input[0] = '\0';
			m_Candidates.clear();
			m_SelectedCandidate = -1;
			m_HistoryPos = static_cast<int>(m_History.size());
			ImGui::SetWindowFocus(nullptr);
		}

		if (submitted)
		{
			// Record in command history (skip blanks and immediate duplicates), then reset the browse
			// position to "past the newest" so the next Up recalls this command.
			if (m_Input[0] != '\0')
			{
				const std::string entry = m_Input;
				if (m_History.empty() || m_History.back() != entry)
				{
					m_History.push_back(entry);
				}
			}
			m_HistoryPos = static_cast<int>(m_History.size());

			ExecuteCommand(m_Input);
			m_Input[0] = '\0';
			m_Candidates.clear();
			m_SelectedCandidate = -1;
			ImGui::SetKeyboardFocusHere(-1); // keep focus for rapid entry
		}

		// --- Suggestions popup: a floating list next to the input, highlighting the selected candidate.
		// Click a row to accept it. Only shown while the input is focused and there are matches.
		if (inputActive && !m_Candidates.empty())
		{
			const int shown = static_cast<int>(m_Candidates.size() < 12 ? m_Candidates.size() : 12);
			const bool hasOverflow = m_Candidates.size() > 12;
			// Estimate the popup height so we can decide whether it fits below the input or must flip above.
			// The input is docked at the very bottom of the screen, so "below" usually overflows off-screen.
			const float lineH = ImGui::GetTextLineHeightWithSpacing();
			const float pad = ImGui::GetStyle().WindowPadding.y * 2.0f;
			const float popupH = pad + lineH * static_cast<float>(shown + (hasOverflow ? 1 : 0));

			const float spaceBelow = ImGui::GetIO().DisplaySize.y - inputMax.y;
			const bool flipAbove = spaceBelow < popupH; // not enough room under the input -> open upward
			const ImVec2 pos = flipAbove ? ImVec2(inputMin.x, inputMin.y - popupH)
			                             : ImVec2(inputMin.x, inputMax.y);

			ImGui::SetNextWindowPos(pos);
			ImGui::SetNextWindowSize(ImVec2(inputMax.x - inputMin.x, 0.0f));
			constexpr ImGuiWindowFlags popupFlags =
			    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_Tooltip;

			ImGui::Begin("##console_suggestions", nullptr, popupFlags);
			for (int i = 0; i < static_cast<int>(m_Candidates.size()) && i < 12; ++i)
			{
				const bool selected = (i == m_SelectedCandidate);
				// Show the CVar's current value/type inline where we can, so the suggestion is informative.
				std::string label = m_Candidates[static_cast<size_t>(i)];
				if (const ICVar* cvar = CVarRegistry::Get().Find(label))
				{
					label += "  = " + cvar->GetValueString() + "  [" + cvar->GetTypeName() + "]";
				}
				if (ImGui::Selectable(label.c_str(), selected))
				{
					const std::string pick = m_Candidates[static_cast<size_t>(i)] + " ";
					std::snprintf(m_Input, sizeof(m_Input), "%s", pick.c_str());
					m_Candidates.clear();
					m_SelectedCandidate = -1;
					ImGui::SetKeyboardFocusHere(-2); // refocus the input next frame
				}
			}
			if (m_Candidates.size() > 12)
			{
				ImGui::TextDisabled("... %zu more", m_Candidates.size() - 12);
			}
			ImGui::End();
		}

		ImGui::End();
	}
}
