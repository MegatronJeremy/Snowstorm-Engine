#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/ShaderManifest.hpp"

using namespace Snowstorm;

namespace
{
	CookedShaderEntry Entry(std::string source, std::vector<std::string> defines, const uint64_t key, const bool debug = false)
	{
		CookedShaderEntry e;
		e.source = std::move(source);
		e.stage = "comp";
		e.defines = std::move(defines);
		e.debug = debug;
		e.key = key;
		e.blob = "b" + std::to_string(key) + ".spv";
		return e;
	}
}

TEST_CASE("ShaderManifest: blob name is content addressed", "[shader]")
{
	const uint8_t a[] = {1, 2, 3, 4};
	const uint8_t b[] = {1, 2, 3, 5};
	REQUIRE(ShaderBlobName(a, sizeof(a)) == ShaderBlobName(a, sizeof(a)));
	REQUIRE(ShaderBlobName(a, sizeof(a)) != ShaderBlobName(b, sizeof(b)));
	const std::string name = ShaderBlobName(a, sizeof(a));
	REQUIRE(name.size() == 16 + 4);
	REQUIRE(name.substr(16) == ".spv");
}

TEST_CASE("ShaderManifest: lookup matches the whole request", "[shader]")
{
	CookedShaderManifest m;
	m.entries = {Entry("Engine/Shaders/GI.comp.hlsl", {"SS_GI_RESTIR", "SS_FP16=1"}, 1),
	             Entry("Engine/Shaders/GI.comp.hlsl", {}, 2),
	             Entry("Engine/Shaders/GI.comp.hlsl", {}, 3, true)};

	REQUIRE(FindCookedShader(m, "Engine/Shaders/GI.comp.hlsl", "comp", {}, false)->key == 2);
	REQUIRE(FindCookedShader(m, "Engine/Shaders/GI.comp.hlsl", "comp", {}, true)->key == 3);
	REQUIRE(FindCookedShader(m, "Engine/Shaders/GI.comp.hlsl", "comp", {"SS_GI_RESTIR", "SS_FP16=1"}, false)->key == 1);
	// Define order is part of the identity, as it is in the cache key.
	REQUIRE(FindCookedShader(m, "Engine/Shaders/GI.comp.hlsl", "comp", {"SS_FP16=1", "SS_GI_RESTIR"}, false) == nullptr);
	REQUIRE(FindCookedShader(m, "Engine/Shaders/GI.comp.hlsl", "frag", {}, false) == nullptr);
	REQUIRE(FindCookedShader(m, "Engine/Shaders/Other.comp.hlsl", "comp", {}, false) == nullptr);
}

TEST_CASE("ShaderManifest: a changed key misses so an edited shader is recompiled", "[shader]")
{
	CookedShaderManifest m;
	m.entries = {Entry("Engine/Shaders/GI.comp.hlsl", {}, 42)};
	REQUIRE(FindCookedShaderByKey(m, "Engine/Shaders/GI.comp.hlsl", "comp", {}, false, 42) != nullptr);
	REQUIRE(FindCookedShaderByKey(m, "Engine/Shaders/GI.comp.hlsl", "comp", {}, false, 43) == nullptr);
}

TEST_CASE("ShaderManifest: json is deterministic after sorting and escapes strings", "[shader]")
{
	CookedShaderManifest a, b;
	a.dxcId = "dxc \"1.8\"\\x";
	b.dxcId = a.dxcId;
	a.entries = {Entry("B.hlsl", {"X=1"}, 0xabc), Entry("A.hlsl", {}, 1)};
	b.entries = {a.entries[1], a.entries[0]};
	SortCookedShaderEntries(a);
	SortCookedShaderEntries(b);
	const std::string json = CookedManifestToJson(a);
	REQUIRE(json == CookedManifestToJson(b));
	REQUIRE(json.find("\"dxcId\": \"dxc \\\"1.8\\\"\\\\x\"") != std::string::npos);
	REQUIRE(json.find("\"key\": \"0000000000000abc\"") != std::string::npos);
	REQUIRE(json.find("\"A.hlsl\"") < json.find("\"B.hlsl\""));
}

TEST_CASE("ShaderManifest: empty manifest is valid json text", "[shader]")
{
	REQUIRE(CookedManifestToJson({}) ==
	        "{\n  \"cookVersion\": 1,\n  \"dxcId\": \"\",\n  \"entries\": []\n}\n");
}
