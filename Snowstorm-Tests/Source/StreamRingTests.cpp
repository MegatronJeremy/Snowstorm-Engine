#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Audio/StreamRing.hpp"

#include <array>
#include <cstdint>
#include <vector>

using namespace Snowstorm;
using PcmFormat = AudioService::PcmFormat;

namespace
{
	std::vector<int16_t> Ramp(const int16_t first, const uint32_t count)
	{
		std::vector<int16_t> v(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			v[i] = static_cast<int16_t>(first + i);
		}
		return v;
	}

	std::vector<int16_t> ReadMono(StreamRing& ring, const uint32_t frames)
	{
		std::vector<int16_t> out(frames, 0x7777);
		ring.Read(out.data(), frames);
		return out;
	}
}

TEST_CASE("StreamRing rejects a degenerate shape", "[audio][streamring]")
{
	CHECK_FALSE(StreamRing(PcmFormat::S16, 0, 16).Valid());
	CHECK_FALSE(StreamRing(PcmFormat::S16, 1, 0).Valid());
	CHECK(StreamRing(PcmFormat::S16, 2, 16).Valid());
}

TEST_CASE("StreamRing frame size follows format and channels", "[audio][streamring]")
{
	CHECK(StreamRing(PcmFormat::U8, 1, 8).BytesPerFrame() == 1);
	CHECK(StreamRing(PcmFormat::S16, 2, 8).BytesPerFrame() == 4);
	CHECK(StreamRing(PcmFormat::F32, 2, 8).BytesPerFrame() == 8);
}

TEST_CASE("StreamRing write beyond capacity returns the accepted count", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 8);
	const auto data = Ramp(1, 20);

	const uint32_t accepted = ring.Write(data.data(), 20);
	CHECK(accepted <= 8);
	CHECK(accepted > 0);
	CHECK(ring.WritableFrames() == 8 - accepted);

	// A full ring takes nothing more, and what was accepted reads back intact and in order.
	while (ring.WritableFrames() > 0)
	{
		REQUIRE(ring.Write(data.data(), 1) == 1);
	}
	CHECK(ring.Write(data.data(), 4) == 0);

	const auto out = ReadMono(ring, accepted);
	for (uint32_t i = 0; i < accepted; ++i)
	{
		CHECK(out[i] == data[i]);
	}
}

TEST_CASE("StreamRing writable frames shrink on write and recover after a read", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 16);
	const auto data = Ramp(100, 10);
	const uint32_t start = ring.WritableFrames();
	REQUIRE(start > 0);
	REQUIRE(start <= 16);

	REQUIRE(ring.Write(data.data(), 10) == 10);
	CHECK(ring.WritableFrames() == start - 10);

	CHECK(ReadMono(ring, 4) == Ramp(100, 4));
	CHECK(ring.WritableFrames() == start - 6);

	CHECK(ReadMono(ring, 6) == Ramp(104, 6));
	CHECK(ring.WritableFrames() == start);
}

TEST_CASE("StreamRing preserves order across the wrap point", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 8);
	int16_t next = 0;
	int16_t expect = 0;
	for (int round = 0; round < 10; ++round)
	{
		const auto chunk = Ramp(next, 5);
		REQUIRE(ring.Write(chunk.data(), 5) == 5);
		next = static_cast<int16_t>(next + 5);
		for (const int16_t v : ReadMono(ring, 5))
		{
			CHECK(v == expect++);
		}
	}
}

TEST_CASE("StreamRing reading an empty ring yields silence", "[audio][streamring]")
{
	SECTION("signed formats are zero")
	{
		StreamRing ring(PcmFormat::S16, 2, 8);
		std::array<int16_t, 8> out{};
		out.fill(0x5555);
		ring.Read(out.data(), 4);
		for (const int16_t v : out)
		{
			CHECK(v == 0);
		}
	}
	SECTION("unsigned 8-bit is centred at 128")
	{
		StreamRing ring(PcmFormat::U8, 1, 8);
		std::array<uint8_t, 4> out{};
		ring.Read(out.data(), 4);
		for (const uint8_t v : out)
		{
			CHECK(v == 0x80);
		}
	}
	SECTION("an underrun pads after the queued frames")
	{
		StreamRing ring(PcmFormat::S16, 1, 8);
		const auto data = Ramp(7, 3);
		REQUIRE(ring.Write(data.data(), 3) == 3);
		const auto out = ReadMono(ring, 6);
		CHECK(out == std::vector<int16_t>{7, 8, 9, 0, 0, 0});
	}
}

TEST_CASE("StreamRing flush discards pending frames on the next read and not before", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 16);
	const auto data = Ramp(1, 10);
	const uint32_t start = ring.WritableFrames();
	REQUIRE(ring.Write(data.data(), 10) == 10);

	ring.RequestFlush();
	// The caller's thread only set a flag: nothing has been consumed yet.
	CHECK(ring.WritableFrames() == start - 10);

	// The next pull discards the queue and plays silence instead.
	CHECK(ReadMono(ring, 4) == std::vector<int16_t>{0, 0, 0, 0});
	CHECK(ring.WritableFrames() == start);

	// The flag was consumed: frames written afterwards survive.
	const auto fresh = Ramp(50, 3);
	REQUIRE(ring.Write(fresh.data(), 3) == 3);
	CHECK(ReadMono(ring, 3) == fresh);
}

TEST_CASE("StreamRing flush on an empty ring is consumed by the next read", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 8);
	ring.RequestFlush();
	CHECK(ReadMono(ring, 2) == std::vector<int16_t>{0, 0});

	const auto data = Ramp(9, 2);
	REQUIRE(ring.Write(data.data(), 2) == 2);
	CHECK(ReadMono(ring, 2) == data);
}

TEST_CASE("StreamRing ignores empty writes", "[audio][streamring]")
{
	StreamRing ring(PcmFormat::S16, 1, 8);
	const auto data = Ramp(1, 2);
	CHECK(ring.Write(nullptr, 2) == 0);
	CHECK(ring.Write(data.data(), 0) == 0);
}
