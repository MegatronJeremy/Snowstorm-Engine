#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/AudioSourceRule.hpp"

using namespace Snowstorm;

TEST_CASE("AudioSource: PlayOnStart plays once, then the latch holds", "[audio]")
{
	AudioSourceInput in{.HasInstance = true, .PlayOnStart = true};
	auto d = DecideAudioSource(in);
	CHECK(d.Play);
	CHECK(d.Started);
	CHECK_FALSE(d.Stop);
	CHECK_FALSE(d.ClearRequests);

	in.Started = true;
	d = DecideAudioSource(in);
	CHECK_FALSE(d.Play);
	CHECK(d.Started);
}

TEST_CASE("AudioSource: StopRequested latches Started so PlayOnStart does not undo it", "[audio]")
{
	const auto d = DecideAudioSource({.HasInstance = true, .StopRequested = true, .PlayOnStart = true});
	CHECK(d.Stop);
	CHECK(d.Started);
	CHECK_FALSE(d.Play);
	CHECK(d.ClearRequests);
}

TEST_CASE("AudioSource: PlayRequested outranks the latch", "[audio]")
{
	const auto d = DecideAudioSource({.HasInstance = true, .PlayRequested = true, .Started = true});
	CHECK(d.Play);
	CHECK(d.Started);
	CHECK_FALSE(d.Stop);
	CHECK(d.ClearRequests);

	// Without PlayOnStart a request still plays, and an unrequested started source does nothing.
	CHECK_FALSE(DecideAudioSource({.HasInstance = true, .Started = true}).Play);
	CHECK_FALSE(DecideAudioSource({.HasInstance = true}).Play);
}

TEST_CASE("AudioSource: Stop and Play together stop first, then play", "[audio]")
{
	const auto d = DecideAudioSource({.HasInstance = true, .PlayRequested = true, .StopRequested = true});
	CHECK(d.Stop);
	CHECK(d.Play);
	CHECK(d.ClearRequests);
}

TEST_CASE("AudioSource: without an instance a request is dropped, never latched", "[audio]")
{
	auto d = DecideAudioSource({.PlayRequested = true, .PlayOnStart = true});
	CHECK(d.ClearRequests);
	CHECK_FALSE(d.Play);
	CHECK_FALSE(d.Stop);
	CHECK_FALSE(d.Started);

	d = DecideAudioSource({.StopRequested = true});
	CHECK(d.ClearRequests);
	CHECK_FALSE(d.Started);

	CHECK_FALSE(DecideAudioSource({.PlayOnStart = true}).ClearRequests);
}

TEST_CASE("AudioSource: requests are cleared only when set", "[audio]")
{
	CHECK_FALSE(DecideAudioSource({.HasInstance = true}).ClearRequests);
	CHECK_FALSE(DecideAudioSource({.HasInstance = true, .PlayOnStart = true}).ClearRequests);
	CHECK(DecideAudioSource({.HasInstance = true, .PlayRequested = true}).ClearRequests);
}

TEST_CASE("AudioSource: sweep keeps only valid entities that still emit", "[audio]")
{
	CHECK(KeepAudioVoice(true, true));
	CHECK_FALSE(KeepAudioVoice(false, true));
	CHECK_FALSE(KeepAudioVoice(true, false));
	CHECK_FALSE(KeepAudioVoice(false, false));
}
