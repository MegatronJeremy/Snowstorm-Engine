#include "ContentBrowserSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Service/EditorTheme.hpp"

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
			if (ec || !it->is_regular_file(ec))
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

		if (ImGui::Button("Rescan"))
		{
			Rescan();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu files", m_Entries.size());
		ImGui::Separator();

		auto& assets = m_World->GetSingleton<AssetManagerSingleton>();
		auto& notify = m_World->GetSingleton<EditorNotificationsSingleton>();

		if (ImGui::BeginChild("##content_list"))
		{
			for (const Entry& entry : m_Entries)
			{
				const bool imported = assets.FindHandle(entry.Path, entry.Type) != 0;

				// Colored type tag.
				ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(entry.Type));
				ImGui::TextUnformatted(AssetTypeToString(entry.Type).c_str());
				ImGui::PopStyleColor();
				ImGui::SameLine(110.0f);

				ImGui::TextUnformatted(entry.DisplayName.c_str());

				ImGui::SameLine();
				ImGui::PushID(entry.Path.c_str());
				if (imported)
				{
					ImGui::BeginDisabled();
					ImGui::SmallButton("Imported");
					ImGui::EndDisabled();
				}
				else if (ImGui::SmallButton("Import"))
				{
					assets.Import(entry.Path, entry.Type);
					assets.SaveRegistry("assets/AssetRegistry.json");
					notify.Push("Imported " + entry.DisplayName, EditorToastType::Success);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
}
