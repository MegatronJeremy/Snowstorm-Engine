#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/ViewportResizeRule.hpp"

using namespace Snowstorm;
using namespace Snowstorm::ViewportResizeRule;

namespace
{
	constexpr Extent kFull{1280, 720};

	Current Built(Extent full, const Extents& e, uint32_t msaa = 1)
	{
		Current c;
		c.HasPresent = c.HasScene = c.HasGI = c.HasAO = c.HasShadow = c.HasMsaaSource = true;
		c.Present = full;
		c.Scene = e.Scene;
		c.GI = e.GI;
		c.AO = e.AO;
		c.Shadow = e.Shadow;
		c.Msaa = msaa;
		return c;
	}
}

TEST_CASE("ViewportResizeRule: ShouldRun skips only when nothing triggers", "[viewport]")
{
	CHECK_FALSE(ShouldRun(false, false, false));
	CHECK(ShouldRun(true, false, false));
	CHECK(ShouldRun(false, true, false));
	CHECK(ShouldRun(false, false, true));
}

TEST_CASE("ViewportResizeRule: ShouldVisit revisits every viewport without ImGui", "[viewport]")
{
	CHECK(ShouldVisit(false, false, false, false));
	CHECK_FALSE(ShouldVisit(true, false, false, false));
	CHECK(ShouldVisit(true, true, false, false));
	CHECK(ShouldVisit(true, false, true, false));
	CHECK(ShouldVisit(true, false, false, true));
}

TEST_CASE("ViewportResizeRule: zero and minimised panel sizes are rejected under ImGui", "[viewport]")
{
	CHECK_FALSE(ResolveSize(true, {0, 0}, {1920, 1080}, 0, 0).Valid);
	CHECK_FALSE(ResolveSize(true, {63, 600}, {1920, 1080}, 0, 0).Valid);
	CHECK_FALSE(ResolveSize(true, {600, 63}, {1920, 1080}, 0, 0).Valid);
	const auto r = ResolveSize(true, {64, 64}, {1920, 1080}, 0, 0);
	CHECK(r.Valid);
	CHECK(r.Size == Extent{64, 64});
}

TEST_CASE("ViewportResizeRule: ImGui ignores the window and the forced resolution", "[viewport]")
{
	const auto r = ResolveSize(true, {800, 600}, {1920, 1080}, 640, 360);
	CHECK(r.Valid);
	CHECK(r.Size == Extent{800, 600});
}

TEST_CASE("ViewportResizeRule: without ImGui the window size is used, even when tiny", "[viewport]")
{
	const auto r = ResolveSize(false, {10, 10}, {1920, 1080}, 0, 0);
	CHECK(r.Valid);
	CHECK(r.Size == Extent{1920, 1080});
	CHECK(ResolveSize(false, {}, {32, 32}, 0, 0).Valid);
}

TEST_CASE("ViewportResizeRule: forced resolution needs both dimensions positive", "[viewport]")
{
	CHECK(ResolveSize(false, {}, {1920, 1080}, 1920, 1080).Size == Extent{1920, 1080});
	CHECK(ResolveSize(false, {}, {1280, 720}, 640, 360).Size == Extent{640, 360});
	CHECK(ResolveSize(false, {}, {1280, 720}, 640, 0).Size == Extent{1280, 720});
	CHECK(ResolveSize(false, {}, {1280, 720}, 0, 360).Size == Extent{1280, 720});
	CHECK(ResolveSize(false, {}, {1280, 720}, -1, -1).Size == Extent{1280, 720});
}

TEST_CASE("ViewportResizeRule: ScaledExtents rounds per target and floors at 1", "[viewport]")
{
	const auto e = ScaledExtents({1280, 720}, {0.5f, 0.25f, 1.0f, 0.1f});
	CHECK(e.Scene == Extent{640, 360});
	CHECK(e.GI == Extent{320, 180});
	CHECK(e.AO == Extent{1280, 720});
	CHECK(e.Shadow == Extent{128, 72});
	const auto tiny = ScaledExtents({1, 1}, {0.1f, 0.1f, 0.1f, 0.1f});
	CHECK(tiny.Scene == Extent{1, 1});
}

TEST_CASE("ViewportResizeRule: NeedsRebuild is a no-op for matching targets", "[viewport]")
{
	const Scales s{0.5f, 0.5f, 0.5f, 0.5f};
	const auto want = ScaledExtents(kFull, s);
	CHECK_FALSE(NeedsRebuild(Built(kFull, want), kFull, want, 1));
}

TEST_CASE("ViewportResizeRule: NeedsRebuild on grow and shrink", "[viewport]")
{
	const Scales s{};
	const auto small = ScaledExtents({640, 360}, s);
	const auto big = ScaledExtents({1920, 1080}, s);
	CHECK(NeedsRebuild(Built({640, 360}, small), {1920, 1080}, big, 1));
	CHECK(NeedsRebuild(Built({1920, 1080}, big), {640, 360}, small, 1));
	CHECK(NeedsRebuild(Built(kFull, ScaledExtents(kFull, s)), {1280, 721}, ScaledExtents({1280, 721}, s), 1));
}

TEST_CASE("ViewportResizeRule: a scale change rebuilds without a size change", "[viewport]")
{
	const auto before = ScaledExtents(kFull, {1.0f, 1.0f, 1.0f, 1.0f});
	const Current cur = Built(kFull, before);
	CHECK(NeedsRebuild(cur, kFull, ScaledExtents(kFull, {0.5f, 1.0f, 1.0f, 1.0f}), 1));
	CHECK(NeedsRebuild(cur, kFull, ScaledExtents(kFull, {1.0f, 0.5f, 1.0f, 1.0f}), 1));
	CHECK(NeedsRebuild(cur, kFull, ScaledExtents(kFull, {1.0f, 1.0f, 0.5f, 1.0f}), 1));
	CHECK(NeedsRebuild(cur, kFull, ScaledExtents(kFull, {1.0f, 1.0f, 1.0f, 0.5f}), 1));
}

TEST_CASE("ViewportResizeRule: MSAA change and missing targets rebuild", "[viewport]")
{
	const auto want = ScaledExtents(kFull, {});
	CHECK(NeedsRebuild(Built(kFull, want, 1), kFull, want, 4));
	CHECK_FALSE(NeedsRebuild(Built(kFull, want, 4), kFull, want, 4));

	Current missing = Built(kFull, want);
	missing.Missing = true;
	CHECK(NeedsRebuild(missing, kFull, want, 1));
	CHECK(NeedsRebuild(Current{.Missing = true}, kFull, want, 1));
}

TEST_CASE("ViewportResizeRule: an unallocated target is never counted as resized", "[viewport]")
{
	const auto want = ScaledExtents(kFull, {});
	CHECK_FALSE(NeedsRebuild(Current{}, kFull, want, 1));
}
