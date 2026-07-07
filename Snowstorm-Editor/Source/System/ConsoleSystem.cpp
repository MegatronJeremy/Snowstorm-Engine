#include "ConsoleSystem.hpp"

#include "Snowstorm/Core/LogBuffer.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <imgui.h>

#include <array>
#include <cstring>
#include <string>

namespace Snowstorm
{
	bool ConsoleSystem::s_Open = false;

	namespace
	{
		constexpr std::array<const char*, 6> kLevelNames = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};

		ImVec4 LevelColor(const LogBuffer::Level level)
		{
			switch (level)
			{
			case LogBuffer::Level::Trace:
				return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			case LogBuffer::Level::Debug:
				return ImVec4(0.55f, 0.70f, 0.90f, 1.0f);
			case LogBuffer::Level::Info:
				return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
			case LogBuffer::Level::Warn:
				return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
			case LogBuffer::Level::Error:
				return ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
			case LogBuffer::Level::Critical:
				return ImVec4(1.00f, 0.25f, 0.25f, 1.0f);
			}
			return ImVec4(1, 1, 1, 1);
		}
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
				ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(e.LevelValue));
				ImGui::TextUnformatted(e.Text.c_str());
				ImGui::PopStyleColor();
			}

			// Autoscroll to the bottom when new lines arrived (or the user is already pinned to the bottom).
			const uint64_t rev = LogBuffer::Get().Revision();
			if (m_AutoScroll && rev != m_LastRevision)
			{
				ImGui::SetScrollHereY(1.0f);
				m_LastRevision = rev;
			}
		}
		ImGui::EndChild();

		// --- Command input: full-width, submits on Enter, keeps focus for rapid entry. ---
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputTextWithHint("##cmd", "Command (help, clear, list, or cvar.name value)...",
		                             m_Input, sizeof(m_Input), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			ExecuteCommand(m_Input);
			m_Input[0] = '\0';
			ImGui::SetKeyboardFocusHere(-1);
		}

		ImGui::End();
	}
}
