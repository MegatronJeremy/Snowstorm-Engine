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
			if (!j.is_array() || j.size() != 4)
				return false;
			out.x = j[0].get<float>();
			out.y = j[1].get<float>();
			out.z = j[2].get<float>();
			out.w = j[3].get<float>();
			return true;
		}

		json Vec3ToJson(const glm::vec3& v)
		{
			return json::array({v.x, v.y, v.z});
		}

		bool JsonToVec3(const json& j, glm::vec3& out)
		{
			if (!j.is_array() || j.size() != 3)
				return false;
			out.x = j[0].get<float>();
			out.y = j[1].get<float>();
			out.z = j[2].get<float>();
			return true;
		}

		// Write a texture handle field only when set (matches the sparse style of the v1 format).
		void WriteHandle(json& root, const char* key, const AssetHandle h)
		{
			if (h != 0)
				root[key] = h.ToString();
		}

		// Read a texture handle field if present (absent -> leave the struct default, i.e. 0).
		void ReadHandle(const json& root, const char* key, AssetHandle& out)
		{
			if (root.contains(key) && root[key].is_string())
				out = UUID::FromString(root[key].get<std::string>());
		}
	}

	bool MaterialAssetIO::Save(const std::filesystem::path& path, const MaterialAsset& asset)
	{
		json root;
		root["Type"] = "SnowstormMaterial";
		root["Version"] = 2;
		root["PipelinePreset"] = PipelinePresetToString(asset.Preset);
		root["BaseColor"] = Vec4ToJson(asset.BaseColor);

		WriteHandle(root, "AlbedoTexture", asset.AlbedoTexture);
		WriteHandle(root, "NormalTexture", asset.NormalTexture);
		WriteHandle(root, "MetallicRoughnessTexture", asset.MetallicRoughnessTexture);
		WriteHandle(root, "AOTexture", asset.AOTexture);
		WriteHandle(root, "EmissiveTexture", asset.EmissiveTexture);

		root["Metallic"] = asset.Metallic;
		root["Roughness"] = asset.Roughness;
		root["EmissiveColor"] = Vec3ToJson(asset.EmissiveColor);

		// Alpha-cutout (glTF MASK). Written unconditionally; absent in old files -> defaults on load.
		root["AlphaMask"] = asset.AlphaMask;
		root["AlphaCutoff"] = asset.AlphaCutoff;

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

		// v2 PBR fields. All optional: a v1 .ssmat (albedo only) leaves these at their struct defaults,
		// so old materials keep loading unchanged.
		ReadHandle(root, "AlbedoTexture", outAsset.AlbedoTexture);
		ReadHandle(root, "NormalTexture", outAsset.NormalTexture);
		ReadHandle(root, "MetallicRoughnessTexture", outAsset.MetallicRoughnessTexture);
		ReadHandle(root, "AOTexture", outAsset.AOTexture);
		ReadHandle(root, "EmissiveTexture", outAsset.EmissiveTexture);

		outAsset.Metallic = root.value("Metallic", outAsset.Metallic);
		outAsset.Roughness = root.value("Roughness", outAsset.Roughness);
		if (root.contains("EmissiveColor"))
			(void)JsonToVec3(root["EmissiveColor"], outAsset.EmissiveColor);

		// Alpha-cutout fields (optional; absent -> struct defaults, so pre-alpha materials stay opaque).
		outAsset.AlphaMask = root.value("AlphaMask", outAsset.AlphaMask);
		outAsset.AlphaCutoff = root.value("AlphaCutoff", outAsset.AlphaCutoff);

		return true;
	}
}
