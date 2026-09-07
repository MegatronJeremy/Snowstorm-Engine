#include "AssetRegistry.hpp"

#include "Snowstorm/Assets/VirtualPath.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	using json = nlohmann::json;

	namespace
	{
		std::filesystem::path NormalizePath(const std::filesystem::path& p)
		{
			return p.lexically_normal();
		}

		// Registry paths used to be stored relative to the PROJECT directory ("assets/meshes/x.obj").
		// A mounted path says the same thing without needing to know which project is active, and can also
		// name engine content, which a project-relative path structurally cannot.
		//
		// The conversion is textual and needs no filesystem: "assets/" is the project's asset directory,
		// which is what /Game/ mounts. Anything already mounted passes through, so this is idempotent and
		// a registry written by a newer build loads unchanged on an older one that still understands the
		// legacy form.
		std::string MigrateToVirtual(const std::string& stored)
		{
			if (stored.empty() || VirtualPath::IsVirtual(stored))
				return stored;

			// The sub-resource suffix is not part of the path and must survive the rewrite.
			const auto ref = VirtualPath::SplitSubResource(stored);
			std::string path = ref.Path;

			constexpr std::string_view assets = "assets/";
			std::string lowered = VirtualPath::NormalizeKey(path);
			if (lowered.starts_with(assets))
				path = "/Game/" + path.substr(assets.size());
			else
				return stored; // outside the asset directory: leave it alone rather than guess a mount

			return ref.SubResource >= 0 ? VirtualPath::JoinSubResource(path, ref.SubResource) : path;
		}

		// Key used to decide whether two paths refer to the same asset. The filesystem is
		// case-insensitive on Windows (assets/Meshes/x.obj == assets/meshes/x.obj), so compare
		// lower-cased generic strings — otherwise the same file gets two handles and shows up
		// twice in the editor. The stored Path keeps its original casing for display.
		//
		// Shared with the virtual path namespace rather than kept separate: an asset's identity key and a
		// mounted path's key have to agree, or a registry lookup and a mount lookup can disagree about
		// whether two spellings name the same file.
		std::string PathKey(const std::filesystem::path& p)
		{
			return VirtualPath::NormalizeKey(NormalizePath(p).generic_string());
		}
	}

	bool AssetRegistry::LoadFromFile(const std::filesystem::path& filePath)
	{
		m_Metadata.clear();

		std::ifstream in(filePath);
		if (!in.is_open())
			return false;

		json root;
		in >> root;

		if (!root.contains("Assets") || !root["Assets"].is_array())
			return false;

		for (const auto& a : root["Assets"])
		{
			const std::string handleStr = a.value("Handle", "0");
			const std::string typeStr = a.value("Type", "None");
			const std::string pathStr = a.value("Path", "");

			if (handleStr == "0" || pathStr.empty())
			{
				continue;
			}

			AssetMetadata m{};
			m.Handle = UUID::FromString(handleStr);
			m.Type = AssetTypeFromString(typeStr);
			m.Path = NormalizePath(MigrateToVirtual(pathStr));

			if (m.Type == AssetType::None || m.Handle == 0)
			{
				continue;
			}

			m_Metadata[m.Handle] = std::move(m);
		}

		return true;
	}

	bool AssetRegistry::SaveToFile(const std::filesystem::path& filePath) const
	{
		json root;
		root["Assets"] = json::array();

		for (const auto& m : m_Metadata | std::views::values)
		{
			json a;
			a["Handle"] = m.Handle.ToString();
			a["Type"] = AssetTypeToString(m.Type);
			a["Path"] = m.Path.generic_string();
			root["Assets"].push_back(std::move(a));
		}

		std::ofstream out(filePath);
		if (!out.is_open())
			return false;

		out << root.dump(2);
		return true;
	}

	AssetHandle AssetRegistry::FindHandleByPath(const std::filesystem::path& assetPath, const AssetType type) const
	{
		const std::string key = PathKey(assetPath);

		for (const auto& m : m_Metadata | std::views::values)
		{
			if (m.Type == type && PathKey(m.Path) == key)
			{
				return m.Handle;
			}
		}

		return AssetHandle{0};
	}

	AssetHandle AssetRegistry::Import(const std::filesystem::path& assetPath, const AssetType type)
	{
		if (AssetHandle existing = FindHandleByPath(assetPath, type); existing.Value() != 0)
		{
			return existing;
		}

		AssetMetadata m{};
		m.Handle = AssetHandle{};
		m.Type = type;
		m.Path = NormalizePath(assetPath);

		m_Metadata[m.Handle] = std::move(m);
		return m.Handle;
	}

	void AssetRegistry::Iterate(const std::function<void(const AssetMetadata&)>& fn) const
	{
		for (const auto& m : m_Metadata | std::views::values)
		{
			fn(m);
		}
	}

	const AssetMetadata* AssetRegistry::GetMetadata(const AssetHandle handle) const
	{
		const auto it = m_Metadata.find(handle);
		if (it == m_Metadata.end())
		{
			return nullptr;
		}
		return &it->second;
	}
}
