#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/PerfBench.hpp"

#include <string>

using namespace Snowstorm;

namespace
{
	// Substring check: the JSON is small and deterministic, so asserting on exact key/value fragments is
	// clearer than parsing. Fixed 4-decimal formatting (see PerfBench.cpp) makes the numbers stable.
	bool Contains(const std::string& hay, const std::string& needle)
	{
		return hay.find(needle) != std::string::npos;
	}
}

TEST_CASE("PerfBench averages per-pass ms over frames", "[render][perfbench]")
{
	PerfBenchAccumulator acc;
	// Forward: 10 then 20 -> avg 15, min 10, max 20. Sky: constant 2.
	acc.AddFrame({{"Forward", 10.0f, 0}, {"Sky", 2.0f, 1}}, 12.0f);
	acc.AddFrame({{"Forward", 20.0f, 0}, {"Sky", 2.0f, 1}}, 22.0f);

	REQUIRE(acc.FrameCount() == 2);
	REQUIRE_FALSE(acc.Empty());

	const std::string json = acc.ToJson({.Device = "TestGPU",
	                                     .Config = "rt-off",
	                                     .TimestampsSupported = true,
	                                     .Width = 1280,
	                                     .Height = 720,
	                                     .CameraPosition = {1.5f, -2.0f, 3.25f},
	                                     .CameraRotation = {0.5f, 0.0f, -0.25f}});

	CHECK(Contains(json, "\"device\": \"TestGPU\""));
	CHECK(Contains(json, "\"config\": \"rt-off\""));
	CHECK(Contains(json, "\"frames\": 2"));
	CHECK(Contains(json, "\"timestampsSupported\": true"));
	CHECK(Contains(json, "\"width\": 1280"));
	CHECK(Contains(json, "\"height\": 720"));
	CHECK(Contains(json, "\"camera\": [1.5000, -2.0000, 3.2500, 0.5000, 0.0000, -0.2500]"));
	// Forward avg = 15, min 10, max 20. fragInvocations 0 (test scopes carry no FS-invocation count).
	CHECK(Contains(json, "\"Forward\": { \"avgMs\": 15.0000, \"minMs\": 10.0000, \"maxMs\": 20.0000, \"fragInvocations\": 0, \"depth\": 0 }"));
	// Sky is a nested scope (depth 1), constant 2.
	CHECK(Contains(json, "\"Sky\": { \"avgMs\": 2.0000, \"minMs\": 2.0000, \"maxMs\": 2.0000, \"fragInvocations\": 0, \"depth\": 1 }"));
	// totalGpuMs = (12+22)/2 = 17.
	CHECK(Contains(json, "\"totalGpuMs\": 17.0000"));
}

TEST_CASE("PerfBench keys are sorted for a stable diff", "[render][perfbench]")
{
	PerfBenchAccumulator acc;
	acc.AddFrame({{"Zulu", 1.0f, 0}, {"Alpha", 1.0f, 0}, {"Mike", 1.0f, 0}}, 3.0f);
	const std::string json = acc.ToJson({.Device = "g", .Config = "c", .TimestampsSupported = true});
	// std::map orders by key, so Alpha precedes Mike precedes Zulu in the output.
	const size_t a = json.find("\"Alpha\"");
	const size_t m = json.find("\"Mike\"");
	const size_t z = json.find("\"Zulu\"");
	CHECK(a != std::string::npos);
	CHECK(a < m);
	CHECK(m < z);
}

TEST_CASE("PerfBench flags unsupported timestamps with no passes", "[render][perfbench]")
{
	// No AddFrame calls (or a device with no timestamp support) -> empty passes, flag false so the
	// runner skips this config instead of reading it as a 0ms regression.
	PerfBenchAccumulator acc;
	REQUIRE(acc.Empty());
	const std::string json = acc.ToJson({.Device = "NoTimestampGPU", .Config = "rt-off", .TimestampsSupported = false});
	CHECK(Contains(json, "\"timestampsSupported\": false"));
	CHECK(Contains(json, "\"passes\": {}"));
	CHECK(Contains(json, "\"frames\": 0"));
}
