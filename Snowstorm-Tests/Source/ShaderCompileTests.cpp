#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/ShaderCompile.hpp"

using namespace Snowstorm;

namespace
{
	DxcRequest Request()
	{
		DxcRequest r;
		r.dxcExe = L"T/dxc.exe";
		r.profile = L"ps_6_5";
		r.input = L"in.hlsl";
		r.output = L"out.spv";
		r.includeDir = L"Inc";
		return r;
	}
}

TEST_CASE("ShaderCompile: plain optimized command line is golden", "[shader]")
{
	REQUIRE(BuildDxcCommandLine(Request()) ==
	        L"\"T/dxc.exe\" -spirv -E main -T ps_6_5 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Zpr"
	        L" -enable-16bit-types -fspv-preserve-bindings -I \"Inc\" -Fo \"out.spv\" \"in.hlsl\"");
}

TEST_CASE("ShaderCompile: defines are emitted in order", "[shader]")
{
	DxcRequest r = Request();
	r.defines = {"SS_GI_RESTIR", "SS_FP16=1"};
	const std::wstring cmd = BuildDxcCommandLine(r);
	const auto a = cmd.find(L" -D SS_GI_RESTIR");
	const auto b = cmd.find(L" -D SS_FP16=1");
	REQUIRE(a != std::wstring::npos);
	REQUIRE(b != std::wstring::npos);
	REQUIRE(a < b);
}

TEST_CASE("ShaderCompile: debug adds source debug info except with ray tracing", "[shader]")
{
	DxcRequest r = Request();
	r.debug = true;
	REQUIRE(BuildDxcCommandLine(r).find(L" -Zi -Od -fspv-debug=vulkan-with-source") != std::wstring::npos);

	r.defines = {"SS_RAYTRACING=1"};
	const std::wstring rt = BuildDxcCommandLine(r);
	REQUIRE(rt.find(L" -Zi -Od") != std::wstring::npos);
	REQUIRE(rt.find(L"-fspv-debug") == std::wstring::npos);
}

TEST_CASE("ShaderCompile: key reacts to every input", "[shader]")
{
	const std::vector<std::string> none;
	const uint64_t base = ComputeShaderKey("src", 1, "v4_ps65", none, false);
	REQUIRE(base == ComputeShaderKey("src", 1, "v4_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("srC", 1, "v4_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 2, "v4_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v4_cs65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v4_ps65", {"SS_FP16=1"}, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v4_ps65", none, true));
}

TEST_CASE("ShaderCompile: key matches the pre-extraction recipe", "[shader]")
{
	// Same XOR-fold the runtime used before the extraction; a change here re-keys every user cache.
	const std::string src = "float4 main() { return 0; }";
	const std::string tag = "v4_vulkan1.2_dxlayout_Zpr_preservebind_ps65";
	const std::string def = "SS_RAYTRACING=1";
	uint64_t expected = ShaderHash64(src.data(), src.size()) ^ 7ull ^ ShaderHash64(tag.data(), tag.size()) ^
	                    ShaderHash64(def.data(), def.size());
	REQUIRE(ComputeShaderKey(src, 7, tag, {def}, false) == expected);
	REQUIRE(ShaderKeyToHex(0xabcull) == "0000000000000abc");
}
