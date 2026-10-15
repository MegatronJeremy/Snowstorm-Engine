#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetImport.hpp"

#include <algorithm>
#include <fstream>
#include <string>

using namespace Snowstorm;
namespace fs = std::filesystem;

namespace
{
	struct TempDir
	{
		fs::path Root;

		TempDir()
		{
			Root = fs::temp_directory_path() / ("ss_assetimport_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			fs::remove_all(Root);
			fs::create_directories(Root / "project");
			fs::create_directories(Root / "outside");
		}

		~TempDir()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}

		fs::path Write(const fs::path& rel, const std::string& content) const
		{
			const fs::path p = Root / rel;
			fs::create_directories(p.parent_path());
			std::ofstream(p, std::ios::binary) << content;
			return p;
		}
	};
} // namespace

TEST_CASE("AssetTypeFromExtension maps by type and ignores case", "[assets][import]")
{
	CHECK(AssetTypeFromExtension(".png") == AssetType::Texture);
	CHECK(AssetTypeFromExtension(".PNG") == AssetType::Texture);
	CHECK(AssetTypeFromExtension(".GlB") == AssetType::Mesh);
	CHECK(AssetTypeFromExtension(".ssmat") == AssetType::Material);
	CHECK(AssetTypeFromExtension(".hlsl") == AssetType::Shader);
	CHECK(AssetTypeFromExtension(".wav") == AssetType::Audio);
	CHECK(AssetTypeFromExtension(".world") == AssetType::Scene);
	CHECK(AssetTypeFromExtension(".dds") == AssetType::None);
	CHECK(AssetTypeFromExtension("") == AssetType::None);
	CHECK(AssetTypeFromExtension("png") == AssetType::None);
	CHECK(AssetTypeFromPath("a/b/Tex.JPEG") == AssetType::Texture);
}

TEST_CASE("ImportableExtensions agrees with AssetTypeFromExtension", "[assets][import]")
{
	const auto exts = ImportableExtensions();
	REQUIRE(!exts.empty());
	for (const char* e : exts)
	{
		CHECK(AssetTypeFromExtension(e) != AssetType::None);
	}
	CHECK(std::none_of(exts.begin(), exts.end(), [](const char* e)
	                   { return std::string(e) == ".dds"; }));
}

TEST_CASE("PlanImport with no project references the source in place", "[assets][import]")
{
	const TempDir t;
	const fs::path src = t.Write("outside/a.png", "x");
	const ImportPlan plan = PlanImport(src, {}, "assets/textures");
	CHECK(!plan.Copy);
	CHECK(plan.DestRel == src);
}

TEST_CASE("PlanImport references a source inside the project project-relative", "[assets][import]")
{
	const TempDir t;
	const fs::path src = t.Write("project/assets/a.png", "x");
	const ImportPlan plan = PlanImport(src, t.Root / "project", "assets/textures");
	CHECK(!plan.Copy);
	CHECK(plan.DestRel.generic_string() == "assets/a.png");
}

TEST_CASE("PlanImport copies an outside source into the destination folder", "[assets][import]")
{
	const TempDir t;
	const fs::path src = t.Write("outside/a.png", "x");
	const ImportPlan plan = PlanImport(src, t.Root / "project", "assets/textures");
	CHECK(plan.Copy);
	CHECK(!plan.Renamed);
	CHECK(!plan.ReuseExisting);
	CHECK(plan.DestRel.generic_string() == "assets/textures/a.png");
}

TEST_CASE("PlanImport suffixes a name collision with different content", "[assets][import]")
{
	const TempDir t;
	const fs::path src = t.Write("outside/a.png", "new");
	t.Write("project/assets/textures/a.png", "old");
	t.Write("project/assets/textures/a_1.png", "older");
	const ImportPlan plan = PlanImport(src, t.Root / "project", "assets/textures");
	CHECK(plan.Copy);
	CHECK(plan.Renamed);
	CHECK(plan.DestRel.generic_string() == "assets/textures/a_2.png");
}

TEST_CASE("PlanImport reuses a byte-identical existing file", "[assets][import]")
{
	const TempDir t;
	const fs::path src = t.Write("outside/a.png", "same");
	t.Write("project/assets/textures/a.png", "same");
	const ImportPlan plan = PlanImport(src, t.Root / "project", "assets/textures");
	CHECK(plan.Copy);
	CHECK(plan.ReuseExisting);
	CHECK(!plan.Renamed);
	CHECK(plan.DestRel.generic_string() == "assets/textures/a.png");
}

TEST_CASE("DefaultImportFolder follows the sample project's layout", "[assets][import]")
{
	CHECK(DefaultImportFolder(AssetType::Mesh) == "meshes");
	CHECK(DefaultImportFolder(AssetType::Texture) == "textures");
	CHECK(DefaultImportFolder(AssetType::Material) == "materials");
	CHECK(DefaultImportFolder(AssetType::Audio) == "audio");
	CHECK(DefaultImportFolder(AssetType::Scene).empty());
	CHECK(DefaultImportFolder(AssetType::None).empty());
}
