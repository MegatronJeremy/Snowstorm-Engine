#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <string>

using namespace Snowstorm;
namespace fs = std::filesystem;

namespace
{
	// A fresh directory per test case, removed on scope exit.
	struct TempDir
	{
		fs::path Root;

		TempDir()
		{
			static std::atomic<int> counter{0};
			Root = fs::temp_directory_path() / ("ss_assetpersist_" + std::to_string(counter++) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			fs::remove_all(Root);
			fs::create_directories(Root);
		}

		~TempDir()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	};

	void WriteText(const fs::path& path, const std::string& text)
	{
		std::ofstream(path) << text;
	}

	size_t CountRows(const AssetRegistry& reg)
	{
		size_t n = 0;
		reg.Iterate([&](const AssetMetadata&)
		            { ++n; });
		return n;
	}
}

TEST_CASE("AssetRegistry save then load preserves handles, mounted paths and types", "[assetregistry]")
{
	TempDir dir;
	const fs::path file = dir.Root / "AssetRegistry.json";

	AssetRegistry reg;
	const AssetHandle mesh = reg.Import("/Game/meshes/cube.obj", AssetType::Mesh);
	const AssetHandle tex = reg.Import("/Game/textures/Checkerboard.png", AssetType::Texture);
	const AssetHandle mat = reg.Import("/Game/materials/Stone.ssmat", AssetType::Material);
	REQUIRE(reg.SaveToFile(file));

	AssetRegistry loaded;
	REQUIRE(loaded.LoadFromFile(file));
	CHECK(CountRows(loaded) == 3);

	const AssetMetadata* m = loaded.GetMetadata(mesh);
	REQUIRE(m != nullptr);
	CHECK(m->Type == AssetType::Mesh);
	CHECK(m->Path.generic_string() == "/Game/meshes/cube.obj");

	const AssetMetadata* t = loaded.GetMetadata(tex);
	REQUIRE(t != nullptr);
	CHECK(t->Type == AssetType::Texture);
	CHECK(t->Path.generic_string() == "/Game/textures/Checkerboard.png");

	const AssetMetadata* s = loaded.GetMetadata(mat);
	REQUIRE(s != nullptr);
	CHECK(s->Type == AssetType::Material);
	CHECK(s->Path.generic_string() == "/Game/materials/Stone.ssmat");

	CHECK(loaded.FindHandleByPath("/Game/meshes/cube.obj", AssetType::Mesh) == mesh);
	CHECK(loaded.FindHandleByPath("assets/textures/Checkerboard.png", AssetType::Texture) == tex);
}

TEST_CASE("AssetRegistry loading twice does not duplicate rows", "[assetregistry]")
{
	TempDir dir;
	const fs::path file = dir.Root / "AssetRegistry.json";

	AssetRegistry reg;
	reg.Import("/Game/meshes/a.obj", AssetType::Mesh);
	reg.Import("/Game/meshes/b.obj", AssetType::Mesh);
	REQUIRE(reg.SaveToFile(file));

	AssetRegistry loaded;
	REQUIRE(loaded.LoadFromFile(file));
	REQUIRE(loaded.LoadFromFile(file));
	CHECK(CountRows(loaded) == 2);

	// Re-importing a loaded asset by either spelling reuses its handle and adds no row.
	const AssetHandle a = loaded.FindHandleByPath("/Game/meshes/a.obj", AssetType::Mesh);
	CHECK(a.Value() != 0);
	CHECK(loaded.Import("assets/meshes/a.obj", AssetType::Mesh) == a);
	CHECK(CountRows(loaded) == 2);
}

TEST_CASE("AssetRegistry load replaces the previous contents", "[assetregistry]")
{
	TempDir dir;
	const fs::path file = dir.Root / "AssetRegistry.json";

	AssetRegistry source;
	const AssetHandle kept = source.Import("/Game/meshes/kept.obj", AssetType::Mesh);
	REQUIRE(source.SaveToFile(file));

	AssetRegistry reg;
	const AssetHandle stale = reg.Import("/Game/meshes/stale.obj", AssetType::Mesh);
	REQUIRE(reg.LoadFromFile(file));

	CHECK(reg.GetMetadata(stale) == nullptr);
	CHECK(reg.GetMetadata(kept) != nullptr);
	CHECK(CountRows(reg) == 1);
}

TEST_CASE("AssetRegistry load migrates legacy project-relative paths and skips unusable rows", "[assetregistry]")
{
	TempDir dir;
	const fs::path file = dir.Root / "AssetRegistry.json";

	nlohmann::json root;
	root["Assets"] = nlohmann::json::array({
	    {{"Handle", "1234"}, {"Type", "Texture"}, {"Path", "assets/textures/legacy.png"}},
	    {{"Handle", "0"}, {"Type", "Mesh"}, {"Path", "/Game/meshes/zero.obj"}},
	    {{"Handle", "5678"}, {"Type", "Mesh"}, {"Path", ""}},
	    {{"Handle", "9999"}, {"Type", "NotAType"}, {"Path", "/Game/meshes/untyped.obj"}},
	});
	WriteText(file, root.dump());

	AssetRegistry reg;
	REQUIRE(reg.LoadFromFile(file));
	CHECK(CountRows(reg) == 1);

	const AssetMetadata* m = reg.GetMetadata(UUID::FromString("1234"));
	REQUIRE(m != nullptr);
	CHECK(m->Type == AssetType::Texture);
	CHECK(m->Path.generic_string() == "/Game/textures/legacy.png");
}

TEST_CASE("AssetRegistry load reports a missing file or a file without Assets", "[assetregistry]")
{
	TempDir dir;
	AssetRegistry reg;
	CHECK_FALSE(reg.LoadFromFile(dir.Root / "missing.json"));

	WriteText(dir.Root / "empty.json", "{}");
	CHECK_FALSE(reg.LoadFromFile(dir.Root / "empty.json"));
}

TEST_CASE("MaterialAssetIO write then read preserves every field", "[materialio]")
{
	TempDir dir;
	const fs::path file = dir.Root / "Full.ssmat";

	MaterialAsset in;
	in.FragmentShader = "/Game/shaders/Custom.frag.hlsl";
	in.BaseColor = glm::vec4(0.25f, 0.5f, 0.75f, 0.125f);
	in.AlbedoTexture = UUID::FromString("1001");
	in.NormalTexture = UUID::FromString("1002");
	in.MetallicRoughnessTexture = UUID::FromString("1003");
	in.AOTexture = UUID::FromString("1004");
	in.EmissiveTexture = UUID::FromString("1005");
	in.Metallic = 0.875f;
	in.Roughness = 0.0625f;
	in.EmissiveColor = glm::vec3(2.0f, 0.5f, 0.25f);
	in.Alpha = AlphaMode::Blend;
	in.AlphaCutoff = 0.375f;
	in.ShaderParams["Zoom"] = glm::vec4(3.0f, 0.0f, 0.0f, 0.0f);
	in.ShaderParams["Tint"] = glm::vec4(1.0f, 0.5f, 0.25f, 0.125f);

	REQUIRE(MaterialAssetIO::Save(file, in));

	MaterialAsset out;
	REQUIRE(MaterialAssetIO::Load(file, out));
	CHECK(out == in);
	CHECK(out.ShaderParams.size() == 2);
}

TEST_CASE("MaterialAssetIO round-trips each alpha mode", "[materialio]")
{
	TempDir dir;
	for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
	{
		const fs::path file = dir.Root / "Alpha.ssmat";
		MaterialAsset in;
		in.Alpha = mode;
		REQUIRE(MaterialAssetIO::Save(file, in));

		MaterialAsset out;
		REQUIRE(MaterialAssetIO::Load(file, out));
		CHECK(out.Alpha == mode);
	}
}

TEST_CASE("MaterialAssetIO writes AlphaMode and sparse fields only", "[materialio]")
{
	TempDir dir;
	const fs::path file = dir.Root / "Sparse.ssmat";

	MaterialAsset in;
	in.Alpha = AlphaMode::Mask;
	REQUIRE(MaterialAssetIO::Save(file, in));

	std::ifstream stream(file);
	const nlohmann::json root = nlohmann::json::parse(stream);

	CHECK(root["AlphaMode"] == "Mask");
	CHECK_FALSE(root.contains("AlphaMask"));
	CHECK_FALSE(root.contains("ShaderParams"));
	CHECK_FALSE(root.contains("AlbedoTexture"));
	CHECK_FALSE(root.contains("NormalTexture"));
	CHECK_FALSE(root.contains("MetallicRoughnessTexture"));
	CHECK_FALSE(root.contains("AOTexture"));
	CHECK_FALSE(root.contains("EmissiveTexture"));
	CHECK(root["Shader"] == kDefaultFragmentShader);
}

TEST_CASE("MaterialAssetIO reads the legacy AlphaMask boolean", "[materialio]")
{
	TempDir dir;

	WriteText(dir.Root / "LegacyMask.ssmat", R"({"Type":"SnowstormMaterial","Version":2,"AlphaMask":true})");
	MaterialAsset masked;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "LegacyMask.ssmat", masked));
	CHECK(masked.Alpha == AlphaMode::Mask);

	WriteText(dir.Root / "LegacyOpaque.ssmat", R"({"Type":"SnowstormMaterial","Version":2,"AlphaMask":false})");
	MaterialAsset opaque;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "LegacyOpaque.ssmat", opaque));
	CHECK(opaque.Alpha == AlphaMode::Opaque);

	// AlphaMode outranks the legacy flag when both are present.
	WriteText(dir.Root / "Both.ssmat", R"({"Type":"SnowstormMaterial","AlphaMode":"Blend","AlphaMask":true})");
	MaterialAsset both;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "Both.ssmat", both));
	CHECK(both.Alpha == AlphaMode::Blend);
}

TEST_CASE("MaterialAssetIO maps a legacy PipelinePreset to a shader path and defaults absent fields", "[materialio]")
{
	TempDir dir;
	WriteText(dir.Root / "V1.ssmat", R"({"Type":"SnowstormMaterial","Version":1,"PipelinePreset":"Mandelbrot"})");

	MaterialAsset out;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "V1.ssmat", out));
	CHECK(out.FragmentShader == "/Engine/Shaders/Mandelbrot.frag.hlsl");
	CHECK(out.BaseColor == MaterialAsset{}.BaseColor);
	CHECK(out.Roughness == MaterialAsset{}.Roughness);
	CHECK(out.AlphaCutoff == MaterialAsset{}.AlphaCutoff);
	CHECK(out.ShaderParams.empty());

	WriteText(dir.Root / "NoShader.ssmat", R"({"Type":"SnowstormMaterial"})");
	MaterialAsset def;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "NoShader.ssmat", def));
	CHECK(def.FragmentShader == kDefaultFragmentShader);
}

TEST_CASE("MaterialAssetIO rejects a missing file and a foreign document type", "[materialio]")
{
	TempDir dir;
	MaterialAsset out;
	CHECK_FALSE(MaterialAssetIO::Load(dir.Root / "missing.ssmat", out));

	WriteText(dir.Root / "Other.ssmat", R"({"Type":"SomethingElse"})");
	CHECK_FALSE(MaterialAssetIO::Load(dir.Root / "Other.ssmat", out));
}

TEST_CASE("MaterialAssetIO skips malformed ShaderParams entries", "[materialio]")
{
	TempDir dir;
	WriteText(dir.Root / "Params.ssmat",
	          R"({"Type":"SnowstormMaterial","ShaderParams":{"Good":[1,2,3,4],"Short":[1,2],"Scalar":5}})");

	MaterialAsset out;
	REQUIRE(MaterialAssetIO::Load(dir.Root / "Params.ssmat", out));
	REQUIRE(out.ShaderParams.size() == 1);
	CHECK(out.ShaderParams.at("Good") == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
}
