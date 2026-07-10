#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Snowstorm;

namespace
{
	std::string HeaderText(const std::vector<uint8_t>& h)
	{
		// The dict starts after the 10-byte preamble (6 magic + 2 version + 2 length).
		return std::string(reinterpret_cast<const char*>(h.data()) + 10, h.size() - 10);
	}
}

// The header must be a valid .npy v1.0 preamble: magic, version 1.0, and a 64-byte-aligned total so the
// payload that follows is aligned (NumPy relies on this).
TEST_CASE("Npy header has valid magic, version, and 64-byte alignment", "[npy]")
{
	const std::vector<uint8_t> h = BuildNpyHeader({4, 4, 4}, NpyDType::Float16);

	REQUIRE(h.size() >= 10);
	CHECK(h[0] == 0x93);
	CHECK(h[1] == 'N');
	CHECK(h[2] == 'U');
	CHECK(h[3] == 'M');
	CHECK(h[4] == 'P');
	CHECK(h[5] == 'Y');
	CHECK(h[6] == 0x01); // major
	CHECK(h[7] == 0x00); // minor

	// The declared dict length (bytes 8-9, little-endian) must equal the actual dict byte count.
	const uint16_t dictLen = static_cast<uint16_t>(h[8] | (h[9] << 8));
	CHECK(static_cast<size_t>(dictLen) == h.size() - 10);

	// Total header block is a multiple of 64, and ends with a newline (NumPy's convention).
	CHECK(h.size() % 64 == 0);
	CHECK(h.back() == '\n');
}

// The dict must describe the dtype and shape we asked for, in C order.
TEST_CASE("Npy header dict encodes dtype and row-major shape", "[npy]")
{
	const std::string f16 = HeaderText(BuildNpyHeader({1059, 1928, 4}, NpyDType::Float16));
	CHECK(f16.find("'descr': '<f2'") != std::string::npos);
	CHECK(f16.find("'fortran_order': False") != std::string::npos);
	CHECK(f16.find("'shape': (1059, 1928, 4, )") != std::string::npos);

	const std::string u8 = HeaderText(BuildNpyHeader({8, 8, 4}, NpyDType::UInt8));
	CHECK(u8.find("'descr': '|u1'") != std::string::npos);
}

// A shape/byteCount mismatch is a bug in the caller — WriteNpy must refuse rather than emit a corrupt file.
TEST_CASE("WriteNpy rejects a shape/byteCount mismatch", "[npy]")
{
	const std::vector<uint16_t> pixels(2 * 2 * 4, 0); // 16 half floats = 32 bytes
	// Claim a 3x3x4 shape (36 elems -> 72 bytes) against a 32-byte payload: inconsistent.
	CHECK_FALSE(WriteNpy("npy_mismatch_should_not_exist.npy", pixels.data(), pixels.size() * 2,
	                     {3, 3, 4}, NpyDType::Float16));
}

// End-to-end: a written file's size is header + payload, and the payload bytes round-trip verbatim.
TEST_CASE("WriteNpy round-trips header + payload to disk", "[npy]")
{
	const std::vector<size_t> shape = {2, 3, 4}; // 24 half floats
	std::vector<uint16_t> pixels(24);
	for (uint16_t i = 0; i < 24; ++i)
	{
		pixels[i] = static_cast<uint16_t>(0x3C00 + i); // arbitrary distinct half-bit patterns
	}
	const size_t bytes = pixels.size() * sizeof(uint16_t);

	const std::string path = "npy_roundtrip_test.npy";
	REQUIRE(WriteNpy(path, pixels.data(), bytes, shape, NpyDType::Float16));

	std::ifstream in(path, std::ios::binary);
	REQUIRE(in);
	const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	const std::vector<uint8_t> header = BuildNpyHeader(shape, NpyDType::Float16);
	REQUIRE(file.size() == header.size() + bytes);

	// Header prefix matches, and the payload after it is byte-identical to what we wrote.
	CHECK(std::equal(header.begin(), header.end(), file.begin()));
	CHECK(std::memcmp(file.data() + header.size(), pixels.data(), bytes) == 0);

	std::remove(path.c_str());
}
