#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/TextureCook.hpp"

#include <atomic>
#include <vector>

using namespace Snowstorm;

namespace
{
	std::vector<uint8_t> Checker2x2(const uint8_t a, const uint8_t b, const uint8_t alphaA = 255, const uint8_t alphaB = 255)
	{
		return {a, a, a, alphaA, b, b, b, alphaB, b, b, b, alphaB, a, a, a, alphaA};
	}
}

TEST_CASE("sRGB mips average in linear light, linear mips average bytes", "[texturecook]")
{
	const auto src = Checker2x2(0, 255, 0, 255);
	const auto srgb = DownsampleRGBA8(src, 2, 2, 1, 1, MipSpace::SRGB);
	const auto lin = DownsampleRGBA8(src, 2, 2, 1, 1, MipSpace::Linear);

	// linear 0.5 encodes to sRGB 188, the byte average is 128
	CHECK(srgb[0] == 188);
	CHECK(srgb[1] == 188);
	CHECK(srgb[2] == 188);
	CHECK(lin[0] == 128);
	// alpha is coverage: a plain average in both spaces
	CHECK(srgb[3] == 128);
	CHECK(lin[3] == 128);
}

TEST_CASE("A constant sRGB colour survives a mip exactly for every byte value", "[texturecook]")
{
	for (int v = 0; v < 256; ++v)
	{
		const std::vector<uint8_t> src(2 * 2 * 4, static_cast<uint8_t>(v));
		const auto mip = DownsampleRGBA8(src, 2, 2, 1, 1, MipSpace::SRGB);
		REQUIRE(mip[0] == v);
		REQUIRE(mip[1] == v);
		REQUIRE(mip[2] == v);
		REQUIRE(mip[3] == v);
	}
}

TEST_CASE("Odd extents reuse the last row and column", "[texturecook]")
{
	// 3x1 -> 1x1: taps are x0=0,x1=1 so texel 2 is not read; height clamps to the single row
	const std::vector<uint8_t> src = {0, 0, 0, 255, 100, 100, 100, 255, 255, 255, 255, 255};
	const auto mip = DownsampleRGBA8(src, 3, 1, 1, 1, MipSpace::Linear);
	CHECK(mip[0] == 50);
}

TEST_CASE("Mip space follows the role, Unknown follows the caller", "[texturecook]")
{
	CHECK(MipSpaceFor(TextureRole::Albedo, false) == MipSpace::SRGB);
	CHECK(MipSpaceFor(TextureRole::Normal, true) == MipSpace::Linear);
	CHECK(MipSpaceFor(TextureRole::Mask, true) == MipSpace::Linear);
	CHECK(MipSpaceFor(TextureRole::Unknown, true) == MipSpace::SRGB);
	CHECK(MipSpaceFor(TextureRole::Unknown, false) == MipSpace::Linear);
}

TEST_CASE("Alpha and normal-map content checks", "[texturecook]")
{
	CHECK_FALSE(UsesAlpha(Checker2x2(10, 20)));
	CHECK(UsesAlpha(Checker2x2(10, 20, 255, 128)));

	const std::vector<uint8_t> flat(4 * 4 * 4, 0);
	CHECK_FALSE(IsTangentSpaceNormalMap(flat)); // length ~1.73, z<0
	std::vector<uint8_t> up;
	for (int i = 0; i < 16; ++i)
		up.insert(up.end(), {128, 128, 255, 255});
	CHECK(IsTangentSpaceNormalMap(up));
}

TEST_CASE("Strip-wise compression equals a single-strip result and aborts", "[texturecook]")
{
	constexpr uint32_t w = 8, h = 40; // 3 strips of 16 rows
	std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
	for (size_t i = 0; i < img.size(); ++i)
		img[i] = static_cast<uint8_t>((i * 37) >> 2);

	const std::atomic<bool> run{false};
	const auto whole = CompressLevel(img, w, h, 1 /* squish::kDxt1 */, run);
	CHECK(whole.size() == (w / 4) * ((h + 3) / 4) * 8);

	const std::atomic<bool> stop{true};
	CHECK(CompressLevel(img, w, h, 1, stop).empty());
}

TEST_CASE("Encoding choice and format mapping", "[texturecook]")
{
	using E = CookedTexture::Encoding;
	CHECK(ChooseEncoding(true, false, true) == E::BC5);
	CHECK(ChooseEncoding(false, true, false) == E::BC3);
	CHECK(ChooseEncoding(false, true, true) == E::BC7);
	CHECK(ChooseEncoding(false, false, false) == E::BC1);
	CHECK(ChooseEncoding(false, false, true) == E::BC7);

	CHECK(PixelFormatFor(E::BC7, true) == PixelFormat::BC7_RGBA_sRGB);
	CHECK(PixelFormatFor(E::BC7, false) == PixelFormat::BC7_RGBA_UNorm);
	CHECK(PixelFormatFor(E::BC5, true) == PixelFormat::BC5_RG_UNorm);
	CHECK(PixelFormatFor(E::BC1, true) == PixelFormat::BC1_RGB_sRGB);
}

TEST_CASE("BC7 strips are 16 bytes per block, keep alpha, and abort", "[texturecook]")
{
	constexpr uint32_t w = 8, h = 40;
	std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
	for (size_t i = 0; i < img.size(); i += 4)
		img[i] = img[i + 1] = img[i + 2] = static_cast<uint8_t>(i >> 2), img[i + 3] = (i / 4) % 2 ? 255 : 0;

	const std::atomic<bool> run{false};
	const auto bc7 = CompressLevelBC7(img, w, h, false, run);
	CHECK(bc7.size() == (w / 4) * (h / 4) * 16);
	CHECK(bc7 == CompressLevelBC7(img, w, h, false, run)); // deterministic

	const std::atomic<bool> stop{true};
	CHECK(CompressLevelBC7(img, w, h, false, stop).empty());
}

TEST_CASE("BC7 searches exhaustively only at the top quality tier", "[texturecook]")
{
	CHECK_FALSE(ExhaustiveBC7Search(0));
	CHECK_FALSE(ExhaustiveBC7Search(1));
	CHECK(ExhaustiveBC7Search(2));
}
