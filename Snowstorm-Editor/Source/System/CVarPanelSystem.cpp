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

		// This panel is the VISUAL editor (checkbox/slider per variable). Typed "name value" commands live
		// in the Console (Debug > Console), which shares its input with the log stream — so the command line
		// that used to be here moved there. Keep this panel focused on browsing + direct widget tweaking.
		ImGui::TextDisabled("Tip: use the Console (Debug > Console) to set variables by name.");

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

				// Startup-only (ReadOnly) CVars are shown but disabled — editing them at runtime has no effect
				// (they're resolved once at launch), so a live widget would be misleading. Mirrors Unreal's
				// ECVF_ReadOnly. The tooltip below tells the user to set it in the startup config + relaunch.
				const bool readOnly = cvar->IsReadOnly();
				if (readOnly)
				{
					ImGui::BeginDisabled();
				}

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

				if (readOnly)
				{
					ImGui::EndDisabled();
				}

				ImGui::SameLine();
				ImGui::TextUnformatted(name.c_str());
				if (ImGui::IsItemHovered() && !cvar->GetDescription().empty())
				{
					// Append a startup-only note so the disabled widget's reason is discoverable on hover.
					const std::string tip = readOnly
					                            ? cvar->GetDescription() + "\n\n[startup-only: set in SnowstormStartup.cfg / CLI and relaunch]"
					                            : cvar->GetDescription();
					ImGui::SetTooltip("%s", tip.c_str());
				}

				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
}
