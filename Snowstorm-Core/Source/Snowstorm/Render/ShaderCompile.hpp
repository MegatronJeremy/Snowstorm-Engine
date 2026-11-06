#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace Snowstorm
{
	// The recipe that turns one HLSL stage into SPIR-V: the dxc command line and the cache key. Pure, with
	// no engine services, so the runtime and a future cook tool share one definition and cannot drift.

	// 64-bit FNV-1a with the same non-standard default seed as Hash64 (see Utility/Hash.hpp), continuing from `seed`.
	[[nodiscard]] inline uint64_t ShaderHash64(const void* data, const size_t size, uint64_t seed = 1469598103934665603ull)
	{
		const auto* p = static_cast<const uint8_t*>(data);
		uint64_t h = seed;
		for (size_t i = 0; i < size; ++i)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	[[nodiscard]] inline std::string ShaderKeyToHex(const uint64_t v)
	{
		char buf[17]{};
		std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
		return {buf};
	}

	// Appends one length-prefixed field to a running key. The prefix keeps field boundaries in the hash
	// ("ab","c" differs from "a","bc") and the chaining makes the result order-sensitive.
	[[nodiscard]] inline uint64_t ShaderKeyAppend(const uint64_t acc, const std::string_view field)
	{
		const uint64_t len = field.size();
		return ShaderHash64(field.data(), field.size(), ShaderHash64(&len, sizeof(len), acc));
	}

	// Folds one include header into the running headers hash; callers pass headers in sorted name order.
	[[nodiscard]] inline uint64_t ShaderHeaderAppend(const uint64_t acc, const std::string_view name,
	                                                 const std::string_view text)
	{
		return ShaderKeyAppend(ShaderKeyAppend(acc, name), text);
	}

	// Cache key (v5) for one stage. headersHash covers every Include/*.hlsli (ShaderHeaderAppend); flagsTag
	// names the stage profile and flag set. Fields are length-prefixed and chained, so define order matters
	// and a define repeated twice does not cancel (the v4 XOR-fold did both).
	[[nodiscard]] inline uint64_t ComputeShaderKey(const std::string_view sourceText, const uint64_t headersHash,
	                                               const std::string_view flagsTag,
	                                               const std::vector<std::string>& defines, const bool debug)
	{
		uint64_t h = 1469598103934665603ull;
		h = ShaderKeyAppend(h, sourceText);
		const std::string_view headers(reinterpret_cast<const char*>(&headersHash), sizeof(headersHash));
		h = ShaderKeyAppend(h, headers);
		h = ShaderKeyAppend(h, flagsTag);
		for (const std::string& def : defines)
			h = ShaderKeyAppend(h, def);
		h = ShaderKeyAppend(h, debug ? "Od" : "");
		return h;
	}

	struct DxcRequest
	{
		std::wstring dxcExe;
		std::wstring profile; // e.g. L"ps_6_5"
		std::wstring input;
		std::wstring output;
		std::wstring includeDir;
		std::vector<std::string> defines; // ASCII macro names, optionally NAME=VALUE
		bool debug = false;
	};

	// Full command line, executable included. Flag rationale:
	//  -enable-16bit-types: float16_t for the SS_FP16 permutation; without it the type widens to fp32.
	//  -fspv-preserve-bindings: the reflected layout must not depend on the optimization level, or flipping
	//    render.shaders.debug would change descriptor layouts the pipeline hot-reload cannot swap.
	//  debug: -Zi -Od, plus -fspv-debug=vulkan-with-source unless SS_RAYTRACING is defined (dxc crashes
	//    combining that debug info with inline ray query).
	[[nodiscard]] inline std::wstring BuildDxcCommandLine(const DxcRequest& r)
	{
		const auto quote = [](const std::wstring& s)
		{ return L"\"" + s + L"\""; };

		std::wstring cmd = quote(r.dxcExe);
		cmd += L" -spirv -E main -T ";
		cmd += r.profile;
		cmd += L" -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Zpr";
		cmd += L" -enable-16bit-types";
		cmd += L" -fspv-preserve-bindings";

		bool rayTracing = false;
		for (const std::string& def : r.defines)
		{
			cmd += L" -D ";
			cmd += std::wstring(def.begin(), def.end());
			if (def == "SS_RAYTRACING=1" || def == "SS_RAYTRACING")
				rayTracing = true;
		}

		if (r.debug)
		{
			cmd += L" -Zi -Od";
			if (!rayTracing)
				cmd += L" -fspv-debug=vulkan-with-source";
		}

		cmd += L" -I " + quote(r.includeDir);
		cmd += L" -Fo " + quote(r.output);
		cmd += L" " + quote(r.input);
		return cmd;
	}
}
