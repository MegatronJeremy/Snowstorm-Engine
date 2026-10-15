#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/MaterialAssetIO.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;

namespace
{
	std::filesystem::path TempPath(const char* name)
	{
		return std::filesystem::temp_directory_path() / name;
	}
}

TEST_CASE("AlphaMode names round-trip and unknown names fall back", "[material][alpha]")
{
	for (const AlphaMode m : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
		CHECK(AlphaModeFromName(AlphaModeName(m)) == m);
	CHECK(AlphaModeFromName("bogus", AlphaMode::Mask) == AlphaMode::Mask);
}

TEST_CASE("MaterialAssetIO persists AlphaMode", "[material][alpha]")
{
	const auto path = TempPath("ss_alpha_blend.ssmat");
	MaterialAsset a;
	a.Alpha = AlphaMode::Blend;
	REQUIRE(MaterialAssetIO::Save(path, a));

	MaterialAsset b;
	REQUIRE(MaterialAssetIO::Load(path, b));
	CHECK(b.Alpha == AlphaMode::Blend);
	std::filesystem::remove(path);
}

TEST_CASE("MaterialAssetIO reads the legacy AlphaMask boolean", "[material][alpha]")
{
	const auto path = TempPath("ss_alpha_legacy.ssmat");
	{
		std::ofstream out(path);
		out << R"({"Type": "SnowstormMaterial", "AlphaMask": true, "AlphaCutoff": 0.25})";
	}
	MaterialAsset a;
	REQUIRE(MaterialAssetIO::Load(path, a));
	CHECK(a.Alpha == AlphaMode::Mask);
	CHECK(a.AlphaCutoff == 0.25f);
	std::filesystem::remove(path);
}
