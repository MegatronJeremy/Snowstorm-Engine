#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>

#include "Snowstorm/Render/AutoExposureMath.hpp"

using namespace Snowstorm::AutoExposure;
using Catch::Approx;

TEST_CASE("auto exposure: luminance outside the histogram range lands in the end bins", "[render][exposure]")
{
	REQUIRE(BinOf(0.0f) == 0);
	REQUIRE(BinOf(-1.0f) == 0);
	REQUIRE(BinOf(std::numeric_limits<float>::quiet_NaN()) == 0);
	REQUIRE(BinOf(std::exp2(kLogMin - 3.0f)) == 0);
	REQUIRE(BinOf(std::exp2(kLogMax + 3.0f)) == kBins - 1);
	REQUIRE(BinOf(std::numeric_limits<float>::infinity()) == kBins - 1);
}

TEST_CASE("auto exposure: a bin's centre luminance falls in that bin", "[render][exposure]")
{
	for (uint32_t bin = 0; bin < kBins; ++bin)
	{
		INFO("bin " << bin);
		REQUIRE(BinOf(std::exp2(BinCentreLog2(bin))) == bin);
	}
}

TEST_CASE("auto exposure: the trimmed mean averages only the window's share of the mass", "[render][exposure]")
{
	Histogram h{};
	REQUIRE_FALSE(TrimmedMeanLog2(h, 0.5f, 0.98f).has_value());

	h[10] = 100;
	REQUIRE(*TrimmedMeanLog2(h, 0.0f, 1.0f) == Approx(BinCentreLog2(10)));
	REQUIRE(*TrimmedMeanLog2(h, 0.5f, 0.98f) == Approx(BinCentreLog2(10)));

	h[20] = 100;
	// Upper half of the mass is exactly bin 20; the middle half takes 50 pixels from each bin.
	REQUIRE(*TrimmedMeanLog2(h, 0.5f, 1.0f) == Approx(BinCentreLog2(20)));
	REQUIRE(*TrimmedMeanLog2(h, 0.25f, 0.75f) == Approx(0.5f * (BinCentreLog2(10) + BinCentreLog2(20))));
	// 0.4..0.6 of 200 pixels: 20 from bin 10, 20 from bin 20.
	REQUIRE(*TrimmedMeanLog2(h, 0.4f, 0.6f) == Approx(0.5f * (BinCentreLog2(10) + BinCentreLog2(20))));
	// 0.3..0.6: 40 from bin 10, 20 from bin 20.
	REQUIRE(*TrimmedMeanLog2(h, 0.3f, 0.6f) == Approx((40.0f * BinCentreLog2(10) + 20.0f * BinCentreLog2(20)) / 60.0f));
}

TEST_CASE("auto exposure: the target maps the mean to middle grey within the clamp", "[render][exposure]")
{
	REQUIRE(TargetLog2(std::log2(kKey), -8.0f, 8.0f) == Approx(0.0f).margin(1e-6));
	REQUIRE(TargetLog2(std::log2(kKey) - 3.0f, -8.0f, 8.0f) == Approx(3.0f));
	REQUIRE(TargetLog2(-20.0f, -8.0f, 8.0f) == 8.0f);
	REQUIRE(TargetLog2(20.0f, -8.0f, 8.0f) == -8.0f);
	// An inverted range collapses to its minimum.
	REQUIRE(TargetLog2(-20.0f, 2.0f, 1.0f) == 2.0f);
}

TEST_CASE("auto exposure: adaptation closes all but 2^-speed of the gap per second", "[render][exposure]")
{
	REQUIRE(AdaptLog2(0.0f, 4.0f, 1.0f, 1.0f, 3.0f) == Approx(2.0f));
	REQUIRE(AdaptLog2(0.0f, -4.0f, 1.0f, 1.0f, 3.0f) == Approx(-3.5f));
	REQUIRE(AdaptLog2(1.0f, 4.0f, 0.0f, 1.0f, 3.0f) == 1.0f);
	REQUIRE(AdaptLog2(1.0f, 4.0f, -1.0f, 1.0f, 3.0f) == 1.0f);
	REQUIRE(AdaptLog2(1.0f, 4.0f, 60.0f, 1.0f, 3.0f) == Approx(4.0f));
	// Two half steps land where one full step does.
	const float half = AdaptLog2(AdaptLog2(0.0f, 4.0f, 0.5f, 1.0f, 3.0f), 4.0f, 0.5f, 1.0f, 3.0f);
	REQUIRE(half == Approx(AdaptLog2(0.0f, 4.0f, 1.0f, 1.0f, 3.0f)));
}

namespace
{
	std::string ShaderConstant(const std::string& text, const std::string& name)
	{
		const std::regex decl("static const (uint|float) " + name + R"( = ([-0-9.]+);)");
		std::smatch m;
		REQUIRE(std::regex_search(text, m, decl));
		return m[2];
	}
}

TEST_CASE("auto exposure: the shader and the C++ mirror use the same constants", "[render][exposure]")
{
	const std::filesystem::path root = SS_REPO_ROOT;
	std::ifstream in(root / "Engine/Shaders/Include/AutoExposure.hlsli");
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();

	REQUIRE(std::stoul(ShaderConstant(text, "AE_BINS")) == kBins);
	REQUIRE(std::stof(ShaderConstant(text, "AE_LOG_MIN")) == kLogMin);
	REQUIRE(std::stof(ShaderConstant(text, "AE_LOG_MAX")) == kLogMax);
	REQUIRE(std::stof(ShaderConstant(text, "AE_KEY")) == kKey);
}
