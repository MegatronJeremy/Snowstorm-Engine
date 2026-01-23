#include "pch.h"
#include "MaterialAssetIO.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	using json = nlohmann::json;

	namespace
	{
		// TODO these functions should be somewhere in engine utilities
		json Vec4ToJson(const glm::vec4& v)
		{
			return json::array({v.x, v.y, v.z, v.w});
		}

		bool JsonToVec4(const json& j, glm::vec4& out)
		{
			if (!j.is_array() || j.size() != 4) return false;
			out.x = j[0].get<float>();
			out.y = j[1].get<float>();
			out.z = j[2].get<float>();
			out.w = j[3].get<float>();
			return true;
		}
	}

	bool MaterialAssetIO::Save(const std::filesystem::path& path, const MaterialAsset& asset)
	{
		json root;
		root["Type"] = "SnowstormMaterial";
		root["Version"] = 1;
		root["PipelinePreset"] = PipelinePresetToString(asset.Preset);
		root["BaseColor"] = Vec4ToJson(asset.BaseColor);

		if (asset.AlbedoTexture != 0)
			root["AlbedoTexture"] = asset.AlbedoTexture.ToString();

		std::ofstream out(path);
		if (!out.is_open())
			return false;

		out << root.dump(2);
		return true;
	}

	bool MaterialAssetIO::Load(const std::filesystem::path& path, MaterialAsset& outAsset)
	{
		std::ifstream in(path);
		if (!in.is_open())
			return false;

		json root;
		in >> root;

		if (root.value("Type", "") != "SnowstormMaterial")
			return false;

		outAsset = MaterialAsset{};
		outAsset.Preset = PipelinePresetFromString(root.value("PipelinePreset", "DefaultLit"));

		if (root.contains("BaseColor"))
			(void)JsonToVec4(root["BaseColor"], outAsset.BaseColor);

		if (root.contains("AlbedoTexture") && root["AlbedoTexture"].is_string())
			outAsset.AlbedoTexture = UUID::FromString(root["AlbedoTexture"].get<std::string>());

		return true;
	}
}
