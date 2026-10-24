#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/AlphaModeRules.hpp"

using namespace Snowstorm;

TEST_CASE("IsSortedTransparentRule needs the CVar, Blend and a built blend pipeline", "[render][alpha]")
{
	CHECK(IsSortedTransparentRule(true, AlphaMode::Blend, true));
}

TEST_CASE("IsSortedTransparentRule is false with render.transparent off", "[render][alpha]")
{
	CHECK_FALSE(IsSortedTransparentRule(false, AlphaMode::Blend, true));
}

TEST_CASE("IsSortedTransparentRule is false for Blend without a pipeline", "[render][alpha]")
{
	CHECK_FALSE(IsSortedTransparentRule(true, AlphaMode::Blend, false));
}

TEST_CASE("IsSortedTransparentRule is false for Mask and Opaque even with a pipeline", "[render][alpha]")
{
	CHECK_FALSE(IsSortedTransparentRule(true, AlphaMode::Mask, true));
	CHECK_FALSE(IsSortedTransparentRule(true, AlphaMode::Opaque, true));
	CHECK_FALSE(IsSortedTransparentRule(true, AlphaMode::Opaque, false));
}

TEST_CASE("IsSortedTransparentRule exhausts the input space with exactly one true", "[render][alpha]")
{
	int trues = 0;
	for (const bool on : {false, true})
		for (const AlphaMode m : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
			for (const bool has : {false, true})
				trues += IsSortedTransparentRule(on, m, has) ? 1 : 0;
	CHECK(trues == 1);
}

TEST_CASE("Only Blend selects the blend pipeline; only Mask sets the shader mask flag", "[render][alpha]")
{
	CHECK_FALSE(AlphaModeUsesBlendPipeline(AlphaMode::Opaque));
	CHECK_FALSE(AlphaModeUsesBlendPipeline(AlphaMode::Mask));
	CHECK(AlphaModeUsesBlendPipeline(AlphaMode::Blend));

	CHECK_FALSE(AlphaModeSetsMaskFlag(AlphaMode::Opaque));
	CHECK(AlphaModeSetsMaskFlag(AlphaMode::Mask));
	CHECK_FALSE(AlphaModeSetsMaskFlag(AlphaMode::Blend));
}
