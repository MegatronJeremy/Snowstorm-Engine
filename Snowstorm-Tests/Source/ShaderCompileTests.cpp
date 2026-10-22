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
	const uint64_t base = ComputeShaderKey("src", 1, "v5_ps65", none, false);
	REQUIRE(base == ComputeShaderKey("src", 1, "v5_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("srC", 1, "v5_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 2, "v5_ps65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v5_cs65", none, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v5_ps65", {"SS_FP16=1"}, false));
	REQUIRE(base != ComputeShaderKey("src", 1, "v5_ps65", none, true));
}

TEST_CASE("ShaderCompile: key is order- and boundary-sensitive", "[shader]")
{
	const uint64_t ab = ComputeShaderKey("s", 1, "t", {"A", "B"}, false);
	REQUIRE(ab != ComputeShaderKey("s", 1, "t", {"B", "A"}, false));
	REQUIRE(ab != ComputeShaderKey("s", 1, "t", {"AB"}, false));
	// A repeated define used to cancel itself under XOR.
	REQUIRE(ComputeShaderKey("s", 1, "t", {"A", "A"}, false) != ComputeShaderKey("s", 1, "t", {}, false));
	REQUIRE(ComputeShaderKey("ab", 1, "c", {}, false) != ComputeShaderKey("a", 1, "bc", {}, false));
}

TEST_CASE("ShaderCompile: headers hash covers names, contents and order", "[shader]")
{
	const uint64_t base = ShaderHeaderAppend(ShaderHeaderAppend(0, "A.hlsli", "x"), "B.hlsli", "y");
	REQUIRE(base == ShaderHeaderAppend(ShaderHeaderAppend(0, "A.hlsli", "x"), "B.hlsli", "y"));
	REQUIRE(base != ShaderHeaderAppend(ShaderHeaderAppend(0, "A.hlsli", "y"), "B.hlsli", "x"));
	REQUIRE(base != ShaderHeaderAppend(ShaderHeaderAppend(0, "B.hlsli", "y"), "A.hlsli", "x"));
	REQUIRE(ShaderKeyToHex(0xabcull) == "0000000000000abc");
}

TEST_CASE("ShaderCompile: cached SPIR-V lookup tries directories in order", "[shader]")
{
	const std::string name = "Mesh.vert_" + ShaderKeyToHex(0xabcull) + ".spv";
	const auto only = [&](const std::string& path)
	{ return [path](const std::string& p)
	  { return p == path; }; };

	REQUIRE(FindCachedSpirv({"cache", "cooked"}, "Mesh.vert", 0xabcull, [](const std::string&)
	                        { return false; })
	            .empty());
	REQUIRE(FindCachedSpirv({"cache", "cooked"}, "Mesh.vert", 0xabcull, only("cooked/" + name)) == "cooked/" + name);
	REQUIRE(FindCachedSpirv({"cache", "cooked"}, "Mesh.vert", 0xabcull, [](const std::string&)
	                        { return true; }) == "cache/" + name);
	REQUIRE(FindCachedSpirv({"cache"}, "Mesh.vert", 0xabcdull, only("cache/" + name)).empty());
	REQUIRE(FindCachedSpirv({}, "Mesh.vert", 0xabcull, [](const std::string&)
	                        { return true; })
	            .empty());
}
