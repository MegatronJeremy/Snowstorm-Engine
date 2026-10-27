#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"
#include "Snowstorm/Assets/ReferenceClosure.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Math/CameraRoute.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

using namespace Snowstorm;

namespace
{
	namespace fs = std::filesystem;

	fs::path AssetsRoot()
	{
		return fs::path(SS_REPO_ROOT) / "Projects" / "Sandbox" / "assets";
	}

	// "/Game/x" is the project's assets directory; resolving by hand keeps the test free of the active-project state
	// VirtualPath needs. A "?submesh=N" suffix selects a part of a model file and is not a path. Registry paths were
	// authored on Windows and differ from the directory names in case (/Game/Textures vs textures), so each
	// component matches case-insensitively, which also keeps the test passing on a case-sensitive filesystem.
	fs::path ResolveGame(const fs::path& mounted)
	{
		std::string s = mounted.generic_string();
		s = s.substr(0, s.find('?'));
		constexpr const char* kPrefix = "/Game/";
		REQUIRE(s.rfind(kPrefix, 0) == 0);

		const auto lower = [](std::string v)
		{
			std::ranges::transform(v, v.begin(), [](const unsigned char c)
			                       { return static_cast<char>(std::tolower(c)); });
			return v;
		};

		fs::path current = AssetsRoot();
		size_t pos = 6;
		while (pos < s.size())
		{
			const size_t end = std::min(s.find('/', pos), s.size());
			const std::string part = s.substr(pos, end - pos);
			pos = end + 1;

			fs::path next = current / part;
			if (!fs::exists(next) && fs::is_directory(current))
			{
				for (const auto& e : fs::directory_iterator(current))
				{
					if (lower(e.path().filename().string()) == lower(part))
					{
						next = e.path();
						break;
					}
				}
			}
			current = next;
		}
		return current;
	}

	std::vector<fs::path> FilesWithExtension(const fs::path& dir, const std::string& ext)
	{
		std::vector<fs::path> out;
		for (const auto& e : fs::directory_iterator(dir))
		{
			if (e.is_regular_file() && e.path().extension() == ext)
				out.push_back(e.path());
		}
		std::ranges::sort(out);
		return out;
	}

	nlohmann::json ReadJson(const fs::path& file)
	{
		std::ifstream in(file);
		REQUIRE(in.is_open());
		return nlohmann::json::parse(in);
	}

	// Decimal strings of 10+ digits in a scene are asset handles or entity UUIDs; small numbers-as-strings are neither.
	void CollectLongDecimals(const nlohmann::json& node, std::set<std::string>& out)
	{
		if (node.is_string())
		{
			const auto& s = node.get_ref<const std::string&>();
			if (s.size() >= 10 && s.size() <= 20 && std::ranges::all_of(s, [](const char c)
			                                                            { return c >= '0' && c <= '9'; }))
				out.insert(s);
		}
		else if (node.is_array() || node.is_object())
		{
			for (const auto& v : node)
				CollectLongDecimals(v, out);
		}
	}
}

TEST_CASE("Every Sandbox scene parses, keeps its components and resolves its asset handles", "[sandbox][content]")
{
	AssetRegistry registry;
	REQUIRE(registry.LoadFromFile(AssetsRoot() / "AssetRegistry.json"));

	const MaterialLoader loadMaterial = [](const AssetMetadata& meta, MaterialAsset& out)
	{ return MaterialAssetIO::Load(ResolveGame(meta.Path), out); };

	const auto scenes = FilesWithExtension(AssetsRoot() / "scenes", ".world");
	REQUIRE(scenes.size() >= 10);

	for (const auto& scenePath : scenes)
	{
		DYNAMIC_SECTION(scenePath.filename().string())
		{
			const nlohmann::json scene = ReadJson(scenePath);
			REQUIRE(scene.contains("Entities"));
			REQUIRE(scene["Entities"].is_array());

			World world;
			REQUIRE(SceneSerializer::Deserialize(world, scenePath.string()));

			// A component name the registry lacks is skipped by the serializer without a word, and the next save
			// deletes it from the file. The Editor registers its own components, which this binary does not link.
			static const std::set<std::string> kEditorOnly = {"MandelbrotControllerComponent"};
			std::set<std::string> entityIds;
			size_t entities = 0;
			for (const auto& ent : scene["Entities"])
			{
				++entities;
				entityIds.insert(ent.value("UUID", ""));
				const auto comps = ent.find("Components");
				if (comps == ent.end())
					continue;
				for (const auto& [name, body] : comps->items())
				{
					const auto& reg = GetComponentRegistry();
					const bool known = kEditorOnly.count(name) != 0 || std::ranges::any_of(reg, [&](const ComponentInfo& ci)
					                                                                       { return ci.Type.is_valid() && ci.Type.get_name().to_string() == name; });
					INFO("entity '" << ent.value("Name", "?") << "' component " << name);
					CHECK(known);
				}
			}
			CHECK(world.GetRegistry().view<entt::entity>().size() >= entities);

			// Every long decimal that is not an entity id must be a registered handle whose file exists.
			std::set<std::string> decimals;
			CollectLongDecimals(scene, decimals);
			for (const auto& d : decimals)
			{
				if (entityIds.count(d))
					continue;
				const AssetMetadata* meta = registry.GetMetadata(UUID::FromString(d));
				INFO("handle " << d << " is not in AssetRegistry.json");
				REQUIRE(meta != nullptr);
				INFO("handle " << d << " names " << meta->Path.generic_string() << ", which is missing on disk");
				CHECK(fs::exists(ResolveGame(meta->Path)));
			}

			ReferenceClosure closure;
			AddSceneToClosure(scene, registry, loadMaterial, closure);
			for (const auto& u : closure.Unresolved)
				FAIL_CHECK(u);
			for (const uint64_t tex : closure.Textures)
			{
				const AssetMetadata* meta = registry.GetMetadata(AssetHandle{tex});
				INFO("material texture handle " << tex << " is not in AssetRegistry.json");
				REQUIRE(meta != nullptr);
				CHECK(fs::exists(ResolveGame(meta->Path)));
			}
		}
	}
}

TEST_CASE("Every Sandbox camera path loads as a route", "[sandbox][content][route]")
{
	const auto paths = FilesWithExtension(AssetsRoot() / "camera-paths", ".json");
	REQUIRE(paths.size() >= 4);

	for (const auto& path : paths)
	{
		DYNAMIC_SECTION(path.filename().string())
		{
			CameraRoute route;
			REQUIRE(LoadCameraRoute(path.string(), route));
			CHECK(route.Waypoints.size() >= 2);
			CHECK(route.Speed > 0.0f);
			CHECK(route.Length() > 0.0f);
		}
	}
}
