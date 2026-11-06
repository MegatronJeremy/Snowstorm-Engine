#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "System/ViewportDisplaySystem.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace Snowstorm;

// The registry is committed, and the editor saves it on every scene save and every content-browser import.
// A save of unchanged contents must therefore be byte-identical, or each one diffs as a content change and
// gets committed by a routine `git add`.

namespace
{
	struct Row
	{
		const char* Handle;
		const char* Type;
		const char* Path;
	};

	std::string RegistryJson(const std::vector<Row>& rows)
	{
		std::string s = "{\"Assets\":[";
		for (size_t i = 0; i < rows.size(); ++i)
		{
			s += i ? "," : "";
			s += std::string("{\"Handle\":\"") + rows[i].Handle + "\",\"Type\":\"" + rows[i].Type + "\",\"Path\":\"" +
			     rows[i].Path + "\"}";
		}
		return s + "]}";
	}

	std::string ReadText(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	AssetRegistry LoadFrom(const std::string& json)
	{
		const std::filesystem::path file = std::filesystem::temp_directory_path() / "ss_registry_serialize_test.json";
		{
			std::ofstream out(file);
			out << json;
		}
		AssetRegistry reg;
		REQUIRE(reg.LoadFromFile(file));
		std::filesystem::remove(file);
		return reg;
	}

	const std::vector<Row> kRows = {
	    {"300", "Texture", "/Game/textures/b.png"},
	    {"100", "Mesh", "/Game/meshes/cube.obj"},
	    {"200", "Material", "/Game/materials/a.ssmat"},
	    {"400", "Texture", "/Game/textures/a.png"},
	};
}

TEST_CASE("Registry output does not depend on insertion order", "[assetregistry]")
{
	const std::vector<Row> reversed(kRows.rbegin(), kRows.rend());
	CHECK(LoadFrom(RegistryJson(kRows)).Serialize() == LoadFrom(RegistryJson(reversed)).Serialize());
}

TEST_CASE("Registry rows are written in path order", "[assetregistry]")
{
	const std::string out = LoadFrom(RegistryJson(kRows)).Serialize();

	const size_t material = out.find("/Game/materials/a.ssmat");
	const size_t mesh = out.find("/Game/meshes/cube.obj");
	const size_t texA = out.find("/Game/textures/a.png");
	const size_t texB = out.find("/Game/textures/b.png");
	REQUIRE(texB != std::string::npos);
	CHECK(material < mesh);
	CHECK(mesh < texA);
	CHECK(texA < texB);
}

TEST_CASE("Saving a loaded registry reproduces it byte for byte", "[assetregistry]")
{
	const AssetRegistry reg = LoadFrom(RegistryJson(kRows));
	const std::string first = reg.Serialize();
	CHECK(LoadFrom(first).Serialize() == first);
	CHECK(first.ends_with("}\n"));

	const std::filesystem::path file = std::filesystem::temp_directory_path() / "ss_registry_save_test.json";
	REQUIRE(reg.SaveToFile(file));
	std::ifstream in(file, std::ios::binary);
	const std::string written{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	in.close();
	std::filesystem::remove(file);
	CHECK(written == first); // LF, not the CRLF a text-mode write produces on Windows
}

// A committed registry out of canonical form is rewritten wholesale by the first editor save.
TEST_CASE("Committed registries are in canonical form", "[assetregistry]")
{
	for (const char* project : {"Sandbox", "Pong"})
	{
		const std::filesystem::path file =
		    std::filesystem::path(SS_REPO_ROOT) / "Projects" / project / "assets" / "AssetRegistry.json";
		AssetRegistry reg;
		INFO(file.generic_string());
		REQUIRE(reg.LoadFromFile(file));
		CHECK(reg.Serialize() == ReadText(file));
	}
}

// The Content Browser auto-imports every file under the project's asset directory, so editor-owned
// content placed there gains a /Game/ registry row on every editor run.
TEST_CASE("The editor icon atlas is engine content", "[assetregistry]")
{
	const std::string path = ViewportDisplaySystem::kIconAtlasPath;
	CHECK(path.starts_with("Engine/"));
	CHECK(std::filesystem::exists(std::filesystem::path(SS_REPO_ROOT) / path));
}
