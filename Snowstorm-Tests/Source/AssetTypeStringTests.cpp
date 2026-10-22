#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetTypes.hpp"

using namespace Snowstorm;

TEST_CASE("AssetType names round-trip through the registry spelling", "[assets]")
{
	for (const AssetType t : {AssetType::Mesh, AssetType::Texture, AssetType::Shader, AssetType::Material, AssetType::Scene, AssetType::Audio})
	{
		REQUIRE(AssetTypeFromString(AssetTypeToString(t)) == t);
	}
}

TEST_CASE("AssetType names are pinned, since they are persisted in the registry", "[assets]")
{
	REQUIRE(AssetTypeToString(AssetType::Mesh) == "Mesh");
	REQUIRE(AssetTypeToString(AssetType::Texture) == "Texture");
	REQUIRE(AssetTypeToString(AssetType::Shader) == "Shader");
	REQUIRE(AssetTypeToString(AssetType::Material) == "Material");
	REQUIRE(AssetTypeToString(AssetType::Scene) == "Scene");
	REQUIRE(AssetTypeToString(AssetType::Audio) == "Audio");
}

TEST_CASE("AssetType None and unknown spellings map to None", "[assets]")
{
	REQUIRE(AssetTypeToString(AssetType::None) == "None");
	REQUIRE(AssetTypeFromString("None") == AssetType::None);
	REQUIRE(AssetTypeFromString("") == AssetType::None);
	REQUIRE(AssetTypeFromString("mesh") == AssetType::None); // case-sensitive
	REQUIRE(AssetTypeFromString("Meshes") == AssetType::None);
	REQUIRE(AssetTypeToString(static_cast<AssetType>(200)) == "None");
}
