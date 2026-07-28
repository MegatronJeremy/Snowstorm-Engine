#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/Exposure.hpp"

#include <cmath>

using namespace Snowstorm;
using Catch::Approx;

// Sunny-16 is the canonical calibration point: f/16, 1/100 s, ISO 100 -> EV100 ~= 14.64. A wrong
// exponent or an ISO/aperture mixup would miss this by whole stops.
TEST_CASE("EV100 from the physical camera triangle matches known values", "[render][exposure]")
{
	CHECK(EV100FromPhysical(16.0f, 1.0f / 100.0f, 100.0f) == Approx(std::log2(256.0f * 100.0f)).epsilon(0.001f));
	CHECK(EV100FromPhysical(16.0f, 1.0f / 100.0f, 100.0f) == Approx(14.6439f).epsilon(0.001f));

	// f/1.4, 1/60 s, ISO 100: log2(1.96 * 60) = log2(117.6) ~= 6.878.
	CHECK(EV100FromPhysical(1.4f, 1.0f / 60.0f, 100.0f) == Approx(std::log2(1.4f * 1.4f * 60.0f)).epsilon(0.001f));
}

// Each stop is a factor of 2 in exactly one of the three controls. Verify the log identities hold so a
// sign flip on any term is caught.
TEST_CASE("EV100 responds by one stop per doubling", "[render][exposure]")
{
	const float base = EV100FromPhysical(8.0f, 1.0f / 100.0f, 100.0f);

	// Doubling ISO gathers twice the light => meter one stop LOWER.
	CHECK(EV100FromPhysical(8.0f, 1.0f / 100.0f, 200.0f) == Approx(base - 1.0f).epsilon(0.001f));
	// Doubling shutter time (1/100 -> 1/50) gathers twice the light => one stop LOWER.
	CHECK(EV100FromPhysical(8.0f, 1.0f / 50.0f, 100.0f) == Approx(base - 1.0f).epsilon(0.001f));
	// Opening one full f-stop (f/8 -> f/5.6, i.e. /sqrt(2) in N) gathers twice the light => one stop LOWER.
	CHECK(EV100FromPhysical(8.0f / std::sqrt(2.0f), 1.0f / 100.0f, 100.0f) == Approx(base - 1.0f).epsilon(0.001f));
}

// Metering: EV100 = log2(L * 100 / K) with K=12.5 => log2(8L). L doubling adds exactly one stop.
TEST_CASE("EV100 from luminance uses the K=12.5 reflected-light constant", "[render][exposure]")
{
	CHECK(EV100FromLuminance(12.5f) == Approx(std::log2(100.0f)).epsilon(0.001f)); // log2(8*12.5)=log2(100)~6.644
	CHECK(EV100FromLuminance(25.0f) == Approx(std::log2(200.0f)).epsilon(0.001f)); // one stop up from 12.5
	CHECK(EV100FromLuminance(100.0f) == Approx(std::log2(800.0f)).epsilon(0.001f));
	// The invariant that matters: doubling luminance adds exactly one stop.
	CHECK(EV100FromLuminance(50.0f) - EV100FromLuminance(25.0f) == Approx(1.0f).epsilon(0.001f));
}

// The exposure multiplier is monotically decreasing in EV100 (brighter scene => smaller multiplier),
// and each stop halves it (Frostbite 1/(1.2*2^EV)).
TEST_CASE("Exposure multiplier halves per stop and is 1/(1.2*2^EV)", "[render][exposure]")
{
	CHECK(ExposureMultiplierFromEV100(0.0f) == Approx(1.0f / 1.2f).epsilon(0.001f));
	const float e0 = ExposureMultiplierFromEV100(10.0f);
	const float e1 = ExposureMultiplierFromEV100(11.0f);
	CHECK(e1 == Approx(e0 * 0.5f).epsilon(0.001f)); // one more stop -> half the multiplier
	CHECK(e0 > e1);                                  // brighter EV -> darker capture
}

// Exposure compensation shifts EV down (positive EC brightens the image).
TEST_CASE("Exposure compensation subtracts stops from EV100", "[render][exposure]")
{
	CHECK(ApplyExposureCompensation(14.0f, 2.0f) == Approx(12.0f));
	CHECK(ApplyExposureCompensation(14.0f, -1.0f) == Approx(15.0f));
	// +1 EC => one stop more exposure => the multiplier doubles.
	const float plain = ExposureMultiplierFromEV100(14.0f);
	const float compd = ExposureMultiplierFromEV100(ApplyExposureCompensation(14.0f, 1.0f));
	CHECK(compd == Approx(plain * 2.0f).epsilon(0.001f));
}

// Temporal adaptation: dt<=0 or speed<=0 snaps; a finite step moves partway and stays bounded between
// start and target; many steps converge to the target.
TEST_CASE("EV adaptation is a bounded exponential approach", "[render][exposure]")
{
	CHECK(AdaptEV(5.0f, 10.0f, 0.0f, 2.0f) == Approx(10.0f)); // dt=0 snaps
	CHECK(AdaptEV(5.0f, 10.0f, 0.016f, 0.0f) == Approx(10.0f)); // speed=0 snaps

	const float stepped = AdaptEV(5.0f, 10.0f, 0.016f, 1.5f);
	CHECK(stepped > 5.0f);  // moved toward target
	CHECK(stepped < 10.0f); // but not past it

	float ev = 5.0f;
	for (int i = 0; i < 600; ++i) // ~10 s at 60 fps
	{
		ev = AdaptEV(ev, 10.0f, 1.0f / 60.0f, 1.5f);
	}
	CHECK(ev == Approx(10.0f).epsilon(0.01f));
}
