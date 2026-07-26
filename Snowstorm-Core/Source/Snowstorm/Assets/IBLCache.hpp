#pragma once

#include "Snowstorm/Lighting/LightingUniforms.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Snowstorm
{
	// The three baked IBL maps, read back from the GPU as raw texel bytes (RGBA16F = 8 B/texel), ready to be
	// re-uploaded via Texture::SetCubeData / SetMipData on a cache hit. Cube maps are face-major:
	// Irradiance[face][mip] / Prefiltered[face][mip], face order matching the bake (+X,-X,+Y,-Y,+Z,-Z). The
	// BRDF LUT is a single 2D blob (environment-independent, but bundled here so one file restores everything).
	struct CookedIBL
	{
		uint32_t IrradianceSize = 0;  // cube edge (px), 1 mip
		uint32_t PrefilteredSize = 0; // cube edge (px) at mip 0
		uint32_t PrefilteredMips = 0; // roughness mip count
		uint32_t BRDFLutSize = 0;     // 2D LUT edge (px)

		std::vector<std::vector<std::vector<uint8_t>>> Irradiance;  // [6][1]
		std::vector<std::vector<std::vector<uint8_t>>> Prefiltered; // [6][mips]
		std::vector<uint8_t> BRDFLut;                               // one 2D level
	};

	// FNV-1a 64-bit hash over the exact fields the IBL bake consumes: the environment sky/ground colors +
	// intensity, and the primary directional light (direction, color, intensity) that lights the captured sky.
	// This is the cache freshness key — two runs with the same sky+sun produce byte-identical maps, so the
	// hash names the on-disk artifact. Pure: no globals, deterministic, unit-testable.
	uint64_t HashIBLEnvironment(const EnvironmentDataBlock& env, const LightDataBlock& lights);

	// Disk cache for baked IBL maps (#34), keyed by the environment hash. Mirrors TextureCacheIO: a versioned
	// header + length-prefixed payload, atomic temp-then-rename save, validated load. A hit lets IBLBakePass
	// skip the ~300 ms cold bake (GPU compute + 4 shader compiles) and just upload the cached bytes.
	class IBLCacheIO
	{
	public:
		// Engine/cache/ibl/<envHash>.ssibl (CWD-relative, gitignored — same convention as the texture/mesh caches).
		static std::filesystem::path GetCachePath(uint64_t envHash);

		// Returns the cooked maps if a fresh, valid, matching-dimensions blob exists for envHash; else nullopt
		// (miss => caller bakes). The expected dimensions guard against a stale file from a different bake config.
		static std::optional<CookedIBL> Load(uint64_t envHash, uint32_t irradianceSize, uint32_t prefilteredSize,
		                                     uint32_t prefilteredMips, uint32_t brdfLutSize);

		// Write the cooked maps for envHash. Atomic (temp + rename). Returns false on any I/O failure (the bake
		// already succeeded on GPU, so a save failure just means no speedup next run — never fatal).
		static bool Save(uint64_t envHash, const CookedIBL& ibl);
	};
}
