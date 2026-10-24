#include "MeshMetaCache.hpp"

#include "Snowstorm/Assets/DerivedData.hpp"
#include "Snowstorm/Assets/SourceHashIndex.hpp"
#include "Snowstorm/Utility/JsonUtils.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kVersion = 2; // content-addressed name
	}

	std::filesystem::path MeshMetaCacheIO::GetCachePath(const uint64_t sourceHash, const int submeshIndex)
	{
		return DerivedDataPath("mesh", DerivedDataKey(sourceHash, kVersion, std::as_bytes(std::span(&submeshIndex, 1))), ".json");
	}

	std::optional<MeshBounds> MeshMetaCacheIO::Load(const std::filesystem::path& sourcePath, const int submeshIndex)
	{
		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return std::nullopt;

		std::ifstream in(GetCachePath(sourceHash, submeshIndex));
		if (!in.is_open())
			return std::nullopt;

		const json root = json::parse(in, nullptr, /*allow_exceptions=*/false);
		if (!root.is_object() || root.value("Version", 0u) != kVersion || !root.contains("Bounds") || !root["Bounds"].is_object())
			return std::nullopt;

		MeshBounds bounds{};
		const auto& b = root["Bounds"];
		bool ok = true;
		ok &= b.contains("Min") && JsonToVec(b["Min"], bounds.Box.Min);
		ok &= b.contains("Max") && JsonToVec(b["Max"], bounds.Box.Max);
		ok &= b.contains("SphereCenter") && JsonToVec(b["SphereCenter"], bounds.Sphere.Center);
		ok &= b.contains("SphereRadius") && b["SphereRadius"].is_number();
		if (!ok)
			return std::nullopt;
		bounds.Sphere.Radius = b["SphereRadius"].get<float>();

		return bounds;
	}

	bool MeshMetaCacheIO::Save(const std::filesystem::path& sourcePath, const int submeshIndex, const MeshBounds& bounds)
	{
		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return false;

		json b;
		b["Min"] = VecToJson(bounds.Box.Min);
		b["Max"] = VecToJson(bounds.Box.Max);
		b["SphereCenter"] = VecToJson(bounds.Sphere.Center);
		b["SphereRadius"] = bounds.Sphere.Radius;
		const json root = {{"Version", kVersion}, {"Bounds", std::move(b)}};

		return WriteFileAtomic(GetCachePath(sourceHash, submeshIndex),
		                       [&](std::ostream& out)
		                       { return static_cast<bool>(out << root.dump(2)); });
	}
}
