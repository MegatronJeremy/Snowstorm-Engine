#include "MeshMetaCache.hpp"

#include "Snowstorm/Utility/JsonUtils.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	std::filesystem::path MeshMetaCacheIO::GetCachePath(const AssetHandle handle)
	{
		// assets/cache/mesh/<handle>.json
		std::filesystem::path p = "assets/cache/mesh";
		p /= handle.ToString();
		p += ".json";
		return p;
	}

	std::optional<MeshMetaCache> MeshMetaCacheIO::Load(const AssetHandle handle)
	{
		const auto path = GetCachePath(handle);

		std::ifstream in(path);
		if (!in.is_open())
			return std::nullopt;

		json root;
		in >> root;

		if (!root.is_object())
			return std::nullopt;

		if (!root.contains("Version") || root["Version"].get<uint32_t>() != MeshMetaCache::Version)
			return std::nullopt;

		MeshMetaCache meta{};
		meta.Handle = handle;

		if (root.contains("SourcePath"))
			meta.SourcePath = root["SourcePath"].get<std::string>();

		if (!root.contains("SourceWriteTime"))
			return std::nullopt;
		meta.SourceWriteTime = root["SourceWriteTime"].get<uint64_t>();

		if (!root.contains("Bounds") || !root["Bounds"].is_object())
			return std::nullopt;

		const auto& b = root["Bounds"];
		bool ok = true;
		ok &= b.contains("Min") && JsonToVec(b["Min"], meta.Bounds.Box.Min);
		ok &= b.contains("Max") && JsonToVec(b["Max"], meta.Bounds.Box.Max);
		ok &= b.contains("SphereCenter") && JsonToVec(b["SphereCenter"], meta.Bounds.Sphere.Center);
		ok &= b.contains("SphereRadius");
		if (ok) meta.Bounds.Sphere.Radius = b["SphereRadius"].get<float>();

		if (!ok)
			return std::nullopt;

		return meta;
	}

	bool MeshMetaCacheIO::Save(const MeshMetaCache& meta)
	{
		const auto path = GetCachePath(meta.Handle);
		std::filesystem::create_directories(path.parent_path());

		json root;
		root["Version"] = MeshMetaCache::Version;
		root["Handle"] = meta.Handle.ToString();
		root["SourcePath"] = meta.SourcePath.generic_string();
		root["SourceWriteTime"] = meta.SourceWriteTime;

		json b;
		b["Min"] = VecToJson(meta.Bounds.Box.Min);
		b["Max"] = VecToJson(meta.Bounds.Box.Max);
		b["SphereCenter"] = VecToJson(meta.Bounds.Sphere.Center);
		b["SphereRadius"] = meta.Bounds.Sphere.Radius;
		root["Bounds"] = std::move(b);

		// atomic-ish: write temp then rename
		const auto tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::trunc);
			if (!out.is_open())
				return false;
			out << root.dump(2);
		}

		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			// fallback: try remove + rename
			std::filesystem::remove(path, ec);
			ec.clear();
			std::filesystem::rename(tmp, path, ec);
		}

		return !ec;
	}
}
