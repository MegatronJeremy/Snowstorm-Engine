#include "CVarPanelSystem.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <imgui.h>

#include <cstring>

namespace Snowstorm
{
	bool CVarPanelSystem::s_Open = false;

	void CVarPanelSystem::Execute(Timestep)
	{
		if (!s_Open)
		{
			return; // closed: no ImGui work at all
		}

		ImGui::SetNextWindowSize(ImVec2(420.0f, 460.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Console Variables", &s_Open))
		{
			ImGui::End();
			return;
		}

		auto& registry = CVarRegistry::Get();

		// --- Command line: "cvar.name value" sets a CVar by name (uses the string setter, so it works for
		// every type). Enter submits; unknown names / bad values are reported to the log.
		ImGui::TextDisabled("Set a variable: type \"name value\" and press Enter");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##cvarcmd", m_Command, sizeof(m_Command), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			std::string line = m_Command;
			const size_t sep = line.find(' ');
			if (sep != std::string::npos)
			{
				const std::string name = line.substr(0, sep);
				const std::string value = line.substr(sep + 1);
				if (ICVar* cvar = registry.Find(name))
				{
					cvar->SetFromString(value);
					SS_INFO("CVar '{0}' set to '{1}'", name, cvar->GetValueString());
				}
				else
				{
					SS_WARN("Unknown CVar: '{0}'", name);
				}
			}
			m_Command[0] = '\0';
			ImGui::SetKeyboardFocusHere(-1); // keep focus in the input for rapid entry
		}

		ImGui::Separator();

		// --- Name filter for the list below.
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##cvarfilter", "Filter by name...", m_Filter, sizeof(m_Filter));

		const std::string filter = m_Filter;

		// --- The list: one row per CVar with a type-appropriate, live-editing widget.
		if (ImGui::BeginChild("##cvarlist", ImVec2(0, 0), true))
		{
			for (ICVar* cvar : registry.All())
			{
				const std::string& name = cvar->GetName();
				if (!filter.empty() && name.find(filter) == std::string::npos)
				{
					continue;
				}

				// Unique widget ID per row (names are unique in the registry).
				ImGui::PushID(name.c_str());
				ImGui::SetNextItemWidth(160.0f);

				switch (cvar->GetKind())
				{
				case CVarKind::Bool:
				{
					bool v = cvar->GetBool();
					if (ImGui::Checkbox("##v", &v))
					{
						cvar->SetBool(v);
					}
					break;
				}
				case CVarKind::Int:
				{
					int v = cvar->GetInt();
					if (ImGui::DragInt("##v", &v))
					{
						cvar->SetInt(v);
					}
					break;
				}
				case CVarKind::Float:
				{
					float v = cvar->GetFloat();
					if (ImGui::DragFloat("##v", &v, 0.01f))
					{
						cvar->SetFloat(v);
					}
					break;
				}
				case CVarKind::String:
				{
					// Strings are edited via the command line above (most are startup-only paths); show
					// the value read-only here to keep the list simple.
					ImGui::TextUnformatted(cvar->GetValueString().c_str());
					break;
				}
				}

				ImGui::SameLine();
				ImGui::TextUnformatted(name.c_str());
				if (ImGui::IsItemHovered() && !cvar->GetDescription().empty())
				{
					ImGui::SetTooltip("%s", cvar->GetDescription().c_str());
				}

				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
}
