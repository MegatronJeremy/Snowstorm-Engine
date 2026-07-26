#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/IBLCache.hpp"

#include <filesystem>

using namespace Snowstorm;

namespace
{
	EnvironmentDataBlock MakeEnv()
	{
		EnvironmentDataBlock e{};
		e.SkyZenithColor = {0.1f, 0.2f, 0.45f};
		e.SkyHorizonColor = {0.5f, 0.6f, 0.75f};
		e.GroundColor = {0.12f, 0.11f, 0.10f};
		e.SkyIntensity = 0.3f;
		e.DrawProceduralSky = true;
		return e;
	}

	LightDataBlock MakeLights()
	{
		LightDataBlock l{};
		l.LightCount = 1;
		l.Lights[0].Direction = {1.2f, -1.4f, -0.7f};
		l.Lights[0].Color = {1.0f, 0.97f, 0.9f};
		l.Lights[0].Intensity = 1.0f;
		return l;
	}
}

// Same inputs -> same hash (the cache key must be reproducible across runs), and each bake-relevant field
// must change it (else two visually-different environments would collide onto one cache file -> wrong maps).
TEST_CASE("IBL environment hash is stable and field-sensitive", "[ibl][cache]")
{
	const EnvironmentDataBlock env = MakeEnv();
	const LightDataBlock lights = MakeLights();
	const uint64_t base = HashIBLEnvironment(env, lights);

	// Reproducible.
	CHECK(HashIBLEnvironment(env, lights) == base);
	CHECK(HashIBLEnvironment(MakeEnv(), MakeLights()) == base);

	// Each environment field the bake reads perturbs the hash.
	{
		EnvironmentDataBlock e = env;
		e.SkyZenithColor.x += 0.01f;
		CHECK(HashIBLEnvironment(e, lights) != base);
	}
	{
		EnvironmentDataBlock e = env;
		e.SkyHorizonColor.y += 0.01f;
		CHECK(HashIBLEnvironment(e, lights) != base);
	}
	{
		EnvironmentDataBlock e = env;
		e.GroundColor.z += 0.01f;
		CHECK(HashIBLEnvironment(e, lights) != base);
	}
	{
		EnvironmentDataBlock e = env;
		e.SkyIntensity += 0.01f;
		CHECK(HashIBLEnvironment(e, lights) != base);
	}

	// The sun (direction / color / intensity) is baked into the captured sky, so it's part of the key too.
	{
		LightDataBlock l = lights;
		l.Lights[0].Direction.x += 0.01f;
		CHECK(HashIBLEnvironment(env, l) != base);
	}
	{
		LightDataBlock l = lights;
		l.Lights[0].Intensity += 0.01f;
		CHECK(HashIBLEnvironment(env, l) != base);
	}

	// No-sun keys differently from a present (even zeroed) sun.
	{
		LightDataBlock l = lights;
		l.LightCount = 0;
		CHECK(HashIBLEnvironment(env, l) != base);
	}
}

// Save then Load must reproduce the maps byte-for-byte (the whole point: a hit re-uploads identical texels).
// Load also enforces the dimension guard and the env-hash match.
TEST_CASE("IBL cache round-trips the cooked maps", "[ibl][cache]")
{
	constexpr uint32_t kIrr = 4; // tiny stand-in dims (real bake uses 32/128/256 — irrelevant to the I/O)
	constexpr uint32_t kPref = 4;
	constexpr uint32_t kPrefMips = 3;
	constexpr uint32_t kLut = 4;
	constexpr uint64_t kHash = 0xABCDEF0123456789ull;

	CookedIBL src;
	src.IrradianceSize = kIrr;
	src.PrefilteredSize = kPref;
	src.PrefilteredMips = kPrefMips;
	src.BRDFLutSize = kLut;

	// Fill with a deterministic byte pattern so a mis-ordered face/mip is caught.
	src.Irradiance.assign(6, std::vector<std::vector<uint8_t>>(1));
	for (uint32_t f = 0; f < 6; ++f)
	{
		src.Irradiance[f][0] = {static_cast<uint8_t>(f + 1), static_cast<uint8_t>(0xA0 + f)};
	}
	src.Prefiltered.assign(6, std::vector<std::vector<uint8_t>>(kPrefMips));
	for (uint32_t f = 0; f < 6; ++f)
	{
		for (uint32_t m = 0; m < kPrefMips; ++m)
		{
			src.Prefiltered[f][m] = {static_cast<uint8_t>(f), static_cast<uint8_t>(m), 0x5Cu};
		}
	}
	src.BRDFLut = {0xDE, 0xAD, 0xBE, 0xEF};

	REQUIRE(IBLCacheIO::Save(kHash, src));

	const std::optional<CookedIBL> loaded = IBLCacheIO::Load(kHash, kIrr, kPref, kPrefMips, kLut);
	REQUIRE(loaded.has_value());

	CHECK(loaded->IrradianceSize == kIrr);
	CHECK(loaded->PrefilteredSize == kPref);
	CHECK(loaded->PrefilteredMips == kPrefMips);
	CHECK(loaded->BRDFLutSize == kLut);
	CHECK(loaded->Irradiance == src.Irradiance);
	CHECK(loaded->Prefiltered == src.Prefiltered);
	CHECK(loaded->BRDFLut == src.BRDFLut);

	// Wrong env hash -> miss.
	CHECK_FALSE(IBLCacheIO::Load(kHash ^ 0x1, kIrr, kPref, kPrefMips, kLut).has_value());
	// Dimension mismatch -> miss (guards against a stale file from a different bake config).
	CHECK_FALSE(IBLCacheIO::Load(kHash, kIrr + 1, kPref, kPrefMips, kLut).has_value());

	std::error_code ec;
	std::filesystem::remove(IBLCacheIO::GetCachePath(kHash), ec);
}
