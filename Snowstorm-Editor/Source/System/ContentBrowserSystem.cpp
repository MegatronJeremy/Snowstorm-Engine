#include "ContentBrowserSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Service/EditorTheme.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace Snowstorm
{
	namespace
	{
		// Map a file extension (lowercase, with dot) to an asset type. None means "not importable".
		AssetType TypeFromExtension(const std::string& ext)
		{
			if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
			{
				return AssetType::Mesh;
			}
			if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds" || ext == ".bmp")
			{
				return AssetType::Texture;
			}
			if (ext == ".ssmat")
			{
				return AssetType::Material;
			}
			if (ext == ".hlsl")
			{
				return AssetType::Shader;
			}
			if (ext == ".world")
			{
				return AssetType::Scene;
			}
			return AssetType::None;
		}

		ImVec4 TypeColor(const AssetType t)
		{
			switch (t)
			{
			case AssetType::Mesh:
				return {0.30f, 0.65f, 1.00f, 1.0f}; // blue
			case AssetType::Texture:
				return {0.40f, 0.85f, 0.40f, 1.0f}; // green
			case AssetType::Material:
				return {1.00f, 0.65f, 0.20f, 1.0f}; // amber
			case AssetType::Shader:
				return {0.85f, 0.40f, 0.85f, 1.0f}; // magenta
			case AssetType::Scene:
				return {0.95f, 0.85f, 0.30f, 1.0f}; // yellow
			default:
				return {0.6f, 0.6f, 0.6f, 1.0f};
			}
		}
	}

	void ContentBrowserSystem::Rescan()
	{
		m_Entries.clear();

		const std::filesystem::path root = "assets";
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
		{
			m_Scanned = true;
			return;
		}

		for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
		     it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec)
			{
				continue;
			}

			// Skip assets/cache/: those are generated cooked artifacts (gitignored), not source
			// assets — don't list or import them.
			if (it->is_directory(ec) && it->path().filename() == "cache")
			{
				it.disable_recursion_pending();
				continue;
			}

			if (!it->is_regular_file(ec))
			{
				continue;
			}

			std::string ext = it->path().extension().string();
			std::ranges::transform(ext, ext.begin(), [](const unsigned char c)
			                       { return static_cast<char>(std::tolower(c)); });

			const AssetType type = TypeFromExtension(ext);
			if (type == AssetType::None)
			{
				continue; // skip scenes, json, meta caches, etc.
			}

			Entry entry;
			entry.Path = it->path().generic_string();
			entry.DisplayName = it->path().filename().string();
			entry.Type = type;
			m_Entries.push_back(std::move(entry));
		}

		std::ranges::sort(m_Entries, [](const Entry& a, const Entry& b)
		                  {
			if (a.Type != b.Type) return a.Type < b.Type;
			return a.DisplayName < b.DisplayName; });

		// Auto-import: every discovered source file gets a registry handle so it is immediately
		// usable in the inspector picker. Import is idempotent (case-insensitive dedup), so this is
		// safe to run on every scan and never creates duplicates. This is the trivial current form of
		// a real engine's import step (see CLAUDE.md "Think like a real engine"); the deliberate next
		// step up is a file watcher so this happens without an explicit scan.
		auto& assets = m_World->GetSingleton<AssetManagerSingleton>();
		bool importedAny = false;
		for (const Entry& entry : m_Entries)
		{
			// Scenes are opened by path (double-click), not referenced by handle, so they are not
			// registry assets — don't auto-import them.
			if (entry.Type == AssetType::Scene)
			{
				continue;
			}
			if (assets.FindHandle(entry.Path, entry.Type) == 0)
			{
				assets.Import(entry.Path, entry.Type);
				importedAny = true;
			}
		}
		if (importedAny)
		{
			assets.SaveRegistry("assets/AssetRegistry.json");
		}

		m_Scanned = true;
	}

	void ContentBrowserSystem::Execute(Timestep)
	{
		if (!m_Scanned)
		{
			Rescan();
		}

		ImGui::Begin("Content Browser");
		EditorTheme::SectionHeader("Content Browser");

		// Files under assets/ are auto-imported on scan, so this panel just lists them. Use Rescan
		// after dropping in new files (until a file watcher makes even that unnecessary).
		if (ImGui::Button("Rescan"))
		{
			Rescan();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu files", m_Entries.size());
		ImGui::Separator();

		ImGui::TextDisabled("Double-click a Scene to open it.");
		ImGui::Separator();

		if (ImGui::BeginChild("##content_list"))
		{
			for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i)
			{
				const Entry& entry = m_Entries[i];
				ImGui::PushID(i);

				// Colored type tag.
				ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(entry.Type));
				ImGui::TextUnformatted(AssetTypeToString(entry.Type).c_str());
				ImGui::PopStyleColor();
				ImGui::SameLine(110.0f);

				// Selectable row; double-clicking a Scene opens it.
				ImGui::Selectable(entry.DisplayName.c_str());
				if (entry.Type == AssetType::Scene && ImGui::IsItemHovered() &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					auto& cmds = SingletonView<EditorCommandsSingleton>();
					auto& notify = SingletonView<EditorNotificationsSingleton>();
					if (cmds.OpenScene)
					{
						const bool ok = cmds.OpenScene(entry.Path);
						notify.Push(ok ? "Opened " + entry.DisplayName : "Failed to open " + entry.DisplayName,
						            ok ? EditorToastType::Success : EditorToastType::Error);
					}
				}

				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
}
