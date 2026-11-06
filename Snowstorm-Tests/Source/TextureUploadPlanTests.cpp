#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/TextureUploadPlan.hpp"

using namespace Snowstorm;

TEST_CASE("MipUploadPlan: shared family transitions in the release and needs no acquire", "[texture][upload]")
{
	const MipUploadPlan p = PlanMipUpload(false, false);
	CHECK(p.ReleaseNewLayout == UploadLayout::Final);
	CHECK_FALSE(p.AcquireOnGraphics);
}

TEST_CASE("MipUploadPlan: dedicated transfer queue never changes layout to Final on the transfer queue", "[texture][upload]")
{
	const MipUploadPlan p = PlanMipUpload(true, false);
	CHECK(p.ReleaseNewLayout == UploadLayout::TransferDst);
	CHECK(p.AcquireOnGraphics);
	CHECK(p.AcquireNewLayout == UploadLayout::Final);
}

TEST_CASE("MipUploadPlan: a texture whose ready layout is TRANSFER_DST keeps one layout pair", "[texture][upload]")
{
	const MipUploadPlan p = PlanMipUpload(true, true);
	CHECK(p.ReleaseNewLayout == UploadLayout::Final);
	CHECK(p.AcquireOnGraphics);
}
