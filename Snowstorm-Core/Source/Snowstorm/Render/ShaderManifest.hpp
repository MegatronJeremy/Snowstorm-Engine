#pragma once

#include "Snowstorm/Render/ShaderCompile.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace Snowstorm
{
	// The index of a cooked shader store (#89): which SPIR-V blob answers which compile request. Pure and
	// device-free, so the cook tool writes it and the runtime reads it through one definition. Blobs are
	// content-addressed (`<hash of the SPIR-V>.spv`), so permutations that compile to identical SPIR-V
	// share one file.

	struct CookedShaderEntry
	{
		std::string source;               // engine-relative, e.g. "Engine/Shaders/GI.comp.hlsl"
		std::string stage;                // "vert" | "frag" | "comp"
		std::vector<std::string> defines; // in compile order; order is part of the identity
		bool debug = false;
		uint64_t key = 0;       // ComputeShaderKey of the inputs the cook compiled
		std::string blob;       // ShaderBlobName of the output
		std::string feature;    // permutations-table tag, empty when none
		std::string capability; // e.g. "rt+fp16", "base"
	};

	struct CookedShaderManifest
	{
		uint32_t cookVersion = 1;
		std::string dxcId; // identity of the compiler that produced the blobs
		std::vector<CookedShaderEntry> entries;
	};

	[[nodiscard]] inline std::string ShaderBlobName(const void* spirv, const size_t size)
	{
		return ShaderKeyToHex(ShaderHash64(spirv, size)) + ".spv";
	}

	// Shipped build (no HLSL on disk, so no key can be computed): the request's identity is the tuple.
	[[nodiscard]] inline const CookedShaderEntry* FindCookedShader(const CookedShaderManifest& m, const std::string_view source,
	                                                               const std::string_view stage,
	                                                               const std::vector<std::string>& defines, const bool debug)
	{
		const auto it = std::find_if(m.entries.begin(), m.entries.end(), [&](const CookedShaderEntry& e)
		                             { return e.source == source && e.stage == stage && e.debug == debug && e.defines == defines; });
		return it == m.entries.end() ? nullptr : &*it;
	}

	// Dev build (sources present): a hit must carry the key computed from the current source text, so an
	// edited shader or header misses and falls through to a real compile instead of serving stale SPIR-V.
	[[nodiscard]] inline const CookedShaderEntry* FindCookedShaderByKey(const CookedShaderManifest& m, const std::string_view source,
	                                                                    const std::string_view stage,
	                                                                    const std::vector<std::string>& defines, const bool debug,
	                                                                    const uint64_t key)
	{
		const CookedShaderEntry* e = FindCookedShader(m, source, stage, defines, debug);
		return e && e->key == key ? e : nullptr;
	}

	// Stable order for a deterministic file: source, stage, debug, defines.
	inline void SortCookedShaderEntries(CookedShaderManifest& m)
	{
		std::sort(m.entries.begin(), m.entries.end(), [](const CookedShaderEntry& a, const CookedShaderEntry& b)
		          { return std::tie(a.source, a.stage, a.debug, a.defines) < std::tie(b.source, b.stage, b.debug, b.defines); });
	}

	namespace ShaderManifestDetail
	{
		inline void AppendJsonString(std::string& out, const std::string_view s)
		{
			out += '"';
			for (const char c : s)
			{
				switch (c)
				{
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buf[7]{};
						std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
						out += buf;
					}
					else
						out += c;
				}
			}
			out += '"';
		}
	}

	// shaders.manifest.json text: one entry per line so diffs of two cooks read line by line. Keys are hex
	// strings because JSON numbers cannot carry 64 bits.
	[[nodiscard]] inline std::string CookedManifestToJson(const CookedShaderManifest& m)
	{
		using ShaderManifestDetail::AppendJsonString;
		std::string out = "{\n  \"cookVersion\": " + std::to_string(m.cookVersion) + ",\n  \"dxcId\": ";
		AppendJsonString(out, m.dxcId);
		out += ",\n  \"entries\": [";
		for (size_t i = 0; i < m.entries.size(); ++i)
		{
			const CookedShaderEntry& e = m.entries[i];
			out += i ? ",\n    {" : "\n    {";
			out += "\"source\": ";
			AppendJsonString(out, e.source);
			out += ", \"stage\": ";
			AppendJsonString(out, e.stage);
			out += ", \"defines\": [";
			for (size_t d = 0; d < e.defines.size(); ++d)
			{
				if (d)
					out += ", ";
				AppendJsonString(out, e.defines[d]);
			}
			out += std::string("], \"debug\": ") + (e.debug ? "true" : "false") + ", \"key\": ";
			AppendJsonString(out, ShaderKeyToHex(e.key));
			out += ", \"blob\": ";
			AppendJsonString(out, e.blob);
			out += ", \"feature\": ";
			AppendJsonString(out, e.feature);
			out += ", \"capability\": ";
			AppendJsonString(out, e.capability);
			out += "}";
		}
		out += m.entries.empty() ? "]\n}\n" : "\n  ]\n}\n";
		return out;
	}
}
