#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/QualityCaptureFormat.hpp"

using namespace Snowstorm;
using namespace Snowstorm::QualityCaptureFormat;

TEST_CASE("HDR capture dtype follows the SceneColor format", "[quality]")
{
	REQUIRE(HdrDType(PixelFormat::RGBA16_SFloat) == NpyDType::Float16);
	REQUIRE(HdrDType(PixelFormat::RGBA32_SFloat) == NpyDType::Float32);
	REQUIRE(HdrBytesPerPixel(PixelFormat::RGBA16_SFloat) == 8);
	REQUIRE(HdrBytesPerPixel(PixelFormat::RGBA32_SFloat) == 16);
}

TEST_CASE("HDR capture rejects non-float formats", "[quality]")
{
	REQUIRE_FALSE(HdrDType(PixelFormat::RGBA8_sRGB).has_value());
	REQUIRE(HdrBytesPerPixel(PixelFormat::RGBA8_UNorm) == 0);
}

TEST_CASE("HDR capture path sits beside the LDR one", "[quality]")
{
	REQUIRE(HdrPath("Dataset/q/atrium__rtgi") == "Dataset/q/atrium__rtgi_hdr.npy");
	REQUIRE(HdrPath("Dataset/q/m", "000042") == "Dataset/q/m_f000042_hdr.npy");
}
