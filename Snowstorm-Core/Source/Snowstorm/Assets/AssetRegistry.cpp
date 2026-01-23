#include "pch.h"
#include "AssetRegistry.hpp"

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
			m.Path = NormalizePath(pathStr);

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
		const auto norm = NormalizePath(assetPath);

		for (const auto& m : m_Metadata | std::views::values)
		{
			if (m.Type == type && NormalizePath(m.Path) == norm)
			{
				return m.Handle;
			}
		}

		return AssetHandle{ 0 };
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
