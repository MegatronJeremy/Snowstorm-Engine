#include <catch2/catch_test_macros.hpp>

// Force the profiler on for this test regardless of build config: we're explicitly validating the
// active recorder. (In engine code SS_PROFILE follows SS_DEBUG, set up via Base.hpp/PCH; the test TU
// doesn't pull that in before Instrumentor, so pin it here.)
#define SS_PROFILE 1
#include "Snowstorm/Debug/Instrumentor.hpp"

#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using namespace Snowstorm;

// The profiler's whole reason to exist over the live panel is a CROSS-THREAD timeline, so the one
// property that must hold is: many threads recording at once never corrupt the output. These tests
// exercise the recorder directly (not the frame-boundary driver) via BeginSession/scopes/EndSession.

namespace
{
	// Count top-level trace events by matching the "name" keys — a cheap validity + completeness check
	// that doesn't pull in a JSON library. A corrupt/torn write would fail to parse these evenly.
	size_t CountEvents(const std::string& json)
	{
		size_t count = 0;
		for (size_t pos = json.find("\"name\":"); pos != std::string::npos; pos = json.find("\"name\":", pos + 1))
		{
			++count;
		}
		return count;
	}
}

TEST_CASE("Instrumentor: inactive session records nothing", "[profiler]")
{
	Instrumentor::Get().EndSession(); // ensure clean state
	{
		SS_PROFILE_SCOPE("should-be-dropped");
	}
	// No session began -> no file to validate; just assert we're not active.
	REQUIRE_FALSE(Instrumentor::Get().IsActive());
}

TEST_CASE("Instrumentor: concurrent recording produces valid, complete output", "[profiler]")
{
	const std::string path = "InstrumentorTest.json";
	std::remove(path.c_str());

	constexpr int kThreads = 8;
	constexpr int kScopesPerThread = 500;

	SS_PROFILE_BEGIN_SESSION("test", path);
	REQUIRE(Instrumentor::Get().IsActive());

	std::vector<std::thread> workers;
	workers.reserve(kThreads);
	for (int t = 0; t < kThreads; ++t)
	{
		workers.emplace_back([kScopesPerThread]
		{
			for (int i = 0; i < kScopesPerThread; ++i)
			{
				SS_PROFILE_SCOPE("worker-scope");
			}
		});
	}
	for (std::thread& w : workers)
	{
		w.join();
	}

	SS_PROFILE_END_SESSION();
	REQUIRE_FALSE(Instrumentor::Get().IsActive());

	std::ifstream in(path);
	REQUIRE(in.is_open());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string json = ss.str();

	// Every scope from every thread must be present (no lost/torn writes under contention).
	REQUIRE(CountEvents(json) == static_cast<size_t>(kThreads) * kScopesPerThread);

	// Well-formed envelope.
	REQUIRE(json.rfind("{\"otherData\"", 0) == 0);
	REQUIRE(json.find("\"traceEvents\":[") != std::string::npos);
	REQUIRE(json.back() == '}');

	std::remove(path.c_str());
}

TEST_CASE("Instrumentor: a second session starts clean (no leakage from the first)", "[profiler]")
{
	const std::string path = "InstrumentorTest2.json";
	std::remove(path.c_str());

	SS_PROFILE_BEGIN_SESSION("first", path);
	{
		SS_PROFILE_SCOPE("a");
		SS_PROFILE_SCOPE("b");
	}
	SS_PROFILE_END_SESSION();

	SS_PROFILE_BEGIN_SESSION("second", path);
	{
		SS_PROFILE_SCOPE("c");
	}
	SS_PROFILE_END_SESSION();

	std::ifstream in(path);
	REQUIRE(in.is_open());
	std::stringstream ss;
	ss << in.rdbuf();
	// The second session must contain ONLY its own event, not the first's two.
	REQUIRE(CountEvents(ss.str()) == 1);

	std::remove(path.c_str());
}
