#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Math/JitterInjection.hpp"

#include <glm/gtc/matrix_transform.hpp>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	glm::vec2 ProjectNdc(const glm::mat4& viewProjection, const glm::vec3& world)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
		return {clip.x / clip.w, clip.y / clip.w};
	}
}

TEST_CASE("Jitter gate truth table", "[math][jitter]")
{
	for (int bits = 0; bits < 32; ++bits)
	{
		const bool exportOn = bits & 1;
		const bool datasetJitter = bits & 2;
		const bool taa = bits & 4;
		const bool jitter = bits & 8;
		const bool compare = bits & 16;
		const bool expected = (exportOn && datasetJitter) || taa || (jitter && !compare);
		CAPTURE(bits);
		CHECK(JitterEnabled(exportOn, datasetJitter, taa, jitter, compare) == expected);
	}

	CHECK_FALSE(JitterEnabled(false, false, false, false, false));
	CHECK(JitterEnabled(false, false, false, true, false));
	CHECK_FALSE(JitterEnabled(false, false, false, true, true));
	CHECK(JitterEnabled(false, false, true, false, true));
	CHECK_FALSE(JitterEnabled(true, false, false, false, true));
	CHECK(JitterEnabled(true, true, false, false, true));
}

TEST_CASE("Jitter pixels convert to NDC at the render resolution", "[math][jitter]")
{
	const glm::vec2 a = JitterPixelsToNdc({0.5f, -0.25f}, 1920.0f, 1080.0f);
	CHECK(a.x == Approx(1.0f / 1920.0f));
	CHECK(a.y == Approx(-0.5f / 1080.0f));

	const glm::vec2 b = JitterPixelsToNdc({0.5f, -0.25f}, 960.0f, 540.0f);
	CHECK(b.x == Approx(1.0f / 960.0f));
	CHECK(b.y == Approx(-0.5f / 540.0f));

	// A full pixel always spans 2 / dim, so halving the resolution doubles the NDC shift.
	CHECK(b.x == Approx(a.x * 2.0f));
}

// glm is right-handed here (only depth 0..1 is forced), so clip.w = -z_view. Adding Ndc to P[2][0] adds
// Ndc * z_view to clip.x, which the divide turns into a shift of -Ndc, the same at every depth.
TEST_CASE("Injected jitter shifts a projected point by -Ndc at any depth", "[math][jitter]")
{
	const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 viewProjection = projection * view;
	const glm::vec2 jitterPx{0.3f, -0.4f};

	for (const glm::vec2 res : {glm::vec2(1920.0f, 1080.0f), glm::vec2(640.0f, 360.0f)})
	{
		const CameraJitter result = InjectJitter(projection, view, viewProjection, jitterPx, res.x, res.y);
		CHECK(result.Ndc.x == Approx(jitterPx.x * 2.0f / res.x));
		CHECK(result.Ndc.y == Approx(jitterPx.y * 2.0f / res.y));

		for (const glm::vec3 p : {glm::vec3(0.7f, -0.3f, -2.0f), glm::vec3(3.0f, 1.5f, -40.0f)})
		{
			const glm::vec2 before = ProjectNdc(viewProjection, p);
			const glm::vec2 after = ProjectNdc(result.JitteredViewProjection, p);
			CAPTURE(res.x, p.z);
			CHECK(after.x - before.x == Approx(-result.Ndc.x).margin(1e-5));
			CHECK(after.y - before.y == Approx(-result.Ndc.y).margin(1e-5));
		}
	}
}

TEST_CASE("A target under one pixel leaves the view-projection untouched", "[math][jitter]")
{
	const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
	const glm::mat4 viewProjection = projection * view;

	const glm::vec2 sizes[] = {{0.0f, 0.0f}, {0.5f, 1080.0f}, {1920.0f, 0.99f}};
	for (const glm::vec2 size : sizes)
	{
		const CameraJitter result = InjectJitter(projection, view, viewProjection, {0.4f, 0.4f}, size.x, size.y);
		CHECK(result.JitteredViewProjection == viewProjection);
		CHECK(result.Ndc == glm::vec2(0.0f));
	}

	const CameraJitter exactlyOne = InjectJitter(projection, view, viewProjection, {0.4f, 0.4f}, 1.0f, 1.0f);
	CHECK(exactlyOne.Ndc.x == Approx(0.8f));
}
