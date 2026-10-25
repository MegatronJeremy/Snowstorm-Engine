#include "MeshMetaCache.hpp"

#include "Snowstorm/Utility/JsonUtils.hpp"

#include "Snowstorm/Utility/EnginePaths.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	std::filesystem::path MeshMetaCacheIO::GetCachePath(const AssetHandle handle)
	{
		// Engine/cache/mesh/<handle>.json
		return GetCachePathIn(EngineAssetPath("Engine/cache/mesh"), handle);
	}

	std::filesystem::path MeshMetaCacheIO::GetCachePathIn(const std::filesystem::path& dir, const AssetHandle handle)
	{
		std::filesystem::path p = dir;
		p /= handle.ToString();
		p += ".json";
		return p;
	}

	static std::optional<MeshMetaCache> Parse(const json& root, const AssetHandle handle)
	{
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
		if (ok)
			meta.Bounds.Sphere.Radius = b["SphereRadius"].get<float>();

		if (!ok)
			return std::nullopt;

		return meta;
	}

	std::optional<MeshMetaCache> MeshMetaCacheIO::Load(const AssetHandle handle)
	{
		return LoadFrom(EngineAssetPath("Engine/cache/mesh"), handle);
	}

	std::optional<MeshMetaCache> MeshMetaCacheIO::LoadFrom(const std::filesystem::path& dir, const AssetHandle handle)
	{
		const auto path = GetCachePathIn(dir, handle);

		std::ifstream in(path);
		if (!in.is_open())
			return std::nullopt;

		// A truncated or hand-edited file is a cache miss, never an exception.
		const json root = json::parse(in, nullptr, false);
		if (root.is_discarded())
			return std::nullopt;
		try
		{
			return Parse(root, handle);
		}
		catch (const json::exception&)
		{
			return std::nullopt;
		}
	}

	bool MeshMetaCacheIO::Save(const MeshMetaCache& meta)
	{
		return SaveTo(EngineAssetPath("Engine/cache/mesh"), meta);
	}

	bool MeshMetaCacheIO::SaveTo(const std::filesystem::path& dir, const MeshMetaCache& meta)
	{
		const auto path = GetCachePathIn(dir, meta.Handle);
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
