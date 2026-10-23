#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Systems/CameraRuntimeUpdateSystem.hpp"
#include "Snowstorm/World/World.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <functional>

using namespace Snowstorm;

namespace
{
	constexpr uint64_t kViewportId = 77;

	struct Rig
	{
		World world;
		CameraRuntimeUpdateSystem system{&world};
		entt::entity viewport = entt::null;
		entt::entity camera = entt::null;

		explicit Rig(const glm::vec2 size = {1600.0f, 900.0f})
		{
			auto& reg = world.GetRegistry();
			viewport = reg.create();
			reg.emplace<IDComponent>(viewport).Id = UUID(kViewportId);
			reg.emplace<ViewportComponent>(viewport).Size = size;

			camera = reg.create();
			reg.emplace<TransformComponent>(camera).Position = {1.0f, 2.0f, 5.0f};
			reg.emplace<CameraComponent>(camera);
			reg.emplace<CameraTargetComponent>(camera).TargetViewportUUID = UUID(kViewportId);
		}

		void Frame()
		{
			system.Execute(Timestep{});
			world.GetRegistry().ClearTrackedComponents();
		}

		const CameraRuntimeComponent& Runtime() const
		{
			return world.GetRegistry().Read<CameraRuntimeComponent>(camera);
		}

		bool HasRuntime() const
		{
			return world.GetRegistry().any_of<CameraRuntimeComponent>(camera);
		}

		void SetViewportSize(const glm::vec2 size)
		{
			world.GetRegistry().patch<ViewportComponent>(viewport, [size](ViewportComponent& v)
			                                             { v.Size = size; });
		}

		void PatchCamera(const std::function<void(CameraComponent&)>& fn)
		{
			world.GetRegistry().patch<CameraComponent>(camera, fn);
		}
	};

	void CheckMatNear(const glm::mat4& a, const glm::mat4& b)
	{
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				CHECK(a[c][r] == Catch::Approx(b[c][r]).margin(1e-5));
			}
		}
	}
}

TEST_CASE("CameraRuntimeUpdateSystem builds perspective view, projection and view-projection", "[camera]")
{
	Rig rig;
	rig.Frame();
	REQUIRE(rig.HasRuntime());

	const auto& tr = rig.world.GetRegistry().Read<TransformComponent>(rig.camera);
	const auto& cam = rig.world.GetRegistry().Read<CameraComponent>(rig.camera);
	const glm::mat4 expectedView = glm::inverse(tr.GetTransformMatrix());
	const glm::mat4 expectedProj = glm::perspectiveRH_ZO(cam.PerspectiveFOV, 1600.0f / 900.0f, cam.PerspectiveNear, cam.PerspectiveFar);

	CheckMatNear(rig.Runtime().View, expectedView);
	CheckMatNear(rig.Runtime().Projection, expectedProj);
	CheckMatNear(rig.Runtime().ViewProjection, expectedProj * expectedView);

	// The camera position maps to the view-space origin
	const glm::vec4 eye = rig.Runtime().View * glm::vec4(tr.Position, 1.0f);
	CHECK(eye.x == Catch::Approx(0.0f).margin(1e-5));
	CHECK(eye.y == Catch::Approx(0.0f).margin(1e-5));
	CHECK(eye.z == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("CameraRuntimeUpdateSystem perspective aspect follows the viewport unless fixed", "[camera]")
{
	Rig rig(glm::vec2(800.0f, 400.0f));
	rig.Frame();
	CHECK(rig.Runtime().Projection[1][1] / rig.Runtime().Projection[0][0] == Catch::Approx(2.0f));

	rig.PatchCamera([](CameraComponent& c)
	                { c.FixedAspectRatio = true; c.AspectRatio = 4.0f; });
	rig.Frame();
	CHECK(rig.Runtime().Projection[1][1] / rig.Runtime().Projection[0][0] == Catch::Approx(4.0f));

	// A non-positive fixed aspect falls back to the viewport
	rig.PatchCamera([](CameraComponent& c)
	                { c.AspectRatio = 0.0f; });
	rig.Frame();
	CHECK(rig.Runtime().Projection[1][1] / rig.Runtime().Projection[0][0] == Catch::Approx(2.0f));
}

TEST_CASE("CameraRuntimeUpdateSystem orthographic size is the full view height", "[camera]")
{
	Rig rig(glm::vec2(800.0f, 400.0f));
	rig.PatchCamera([](CameraComponent& c)
	                { c.Projection = CameraComponent::ProjectionType::Orthographic; c.OrthographicSize = 8.0f; });
	rig.Frame();

	const glm::mat4& p = rig.Runtime().Projection;
	CHECK(p[1][1] == Catch::Approx(2.0f / 8.0f));
	CHECK(p[0][0] == Catch::Approx(2.0f / 16.0f)); // width = height * aspect(2)

	const auto& cam = rig.world.GetRegistry().Read<CameraComponent>(rig.camera);
	CheckMatNear(p, glm::ortho(-8.0f, 8.0f, -4.0f, 4.0f, cam.OrthographicNear, cam.OrthographicFar));
	CheckMatNear(rig.Runtime().ViewProjection, p * rig.Runtime().View);
}

TEST_CASE("CameraRuntimeUpdateSystem frustum matches the view-projection", "[camera]")
{
	Rig rig;
	rig.Frame();

	// Looking down -Z from (1,2,5): a point straight ahead is inside, one behind the camera is outside
	CHECK(rig.Runtime().frustum.IntersectsSphere(glm::vec3(1.0f, 2.0f, 0.0f), 0.1f));
	CHECK_FALSE(rig.Runtime().frustum.IntersectsSphere(glm::vec3(1.0f, 2.0f, 50.0f), 0.1f));
	CHECK_FALSE(rig.Runtime().frustum.IntersectsSphere(glm::vec3(101.0f, 2.0f, 0.0f), 0.1f));
}

TEST_CASE("CameraRuntimeUpdateSystem does not touch an unchanged camera", "[camera]")
{
	Rig rig;
	rig.Frame();
	REQUIRE(rig.HasRuntime());

	// Marker survives only if the system skips the camera: a rewrite would reset it
	rig.world.GetRegistry().Write<CameraRuntimeComponent>(rig.camera).View = glm::mat4(7.0f);
	rig.world.GetRegistry().ClearTrackedComponents();

	rig.Frame();
	CHECK(rig.Runtime().View[0][0] == 7.0f);
}

TEST_CASE("CameraRuntimeUpdateSystem recomputes on camera, transform and viewport changes", "[camera]")
{
	Rig rig;
	rig.Frame();

	SECTION("transform")
	{
		rig.world.GetRegistry().patch<TransformComponent>(rig.camera, [](TransformComponent& t)
		                                                  { t.Position = {0.0f, 0.0f, 10.0f}; });
		rig.Frame();
		CHECK(rig.Runtime().View[3][2] == Catch::Approx(-10.0f));
	}

	SECTION("camera config")
	{
		rig.PatchCamera([](CameraComponent& c)
		                { c.PerspectiveFOV = 1.5f; });
		rig.Frame();
		CHECK(rig.Runtime().Projection[1][1] == Catch::Approx(1.0f / std::tan(0.75f)));
	}

	SECTION("viewport resize")
	{
		rig.SetViewportSize({400.0f, 400.0f});
		rig.Frame();
		CHECK(rig.Runtime().Projection[1][1] == Catch::Approx(rig.Runtime().Projection[0][0]));
	}
}

TEST_CASE("CameraRuntimeUpdateSystem skips cameras without a usable target", "[camera]")
{
	SECTION("unresolvable viewport UUID")
	{
		Rig rig;
		rig.world.GetRegistry().patch<CameraTargetComponent>(rig.camera, [](CameraTargetComponent& t)
		                                                     { t.TargetViewportUUID = UUID(kViewportId + 1); });
		rig.Frame();
		CHECK_FALSE(rig.HasRuntime());
	}

	SECTION("viewport smaller than one pixel")
	{
		Rig rig(glm::vec2(0.0f, 0.0f));
		rig.Frame();
		// The component is emplaced before the size check, so it exists but keeps its defaults
		REQUIRE(rig.HasRuntime());
		const glm::mat4 identity(1.0f);
		CheckMatNear(rig.Runtime().View, identity);
		CheckMatNear(rig.Runtime().Projection, identity);
		CheckMatNear(rig.Runtime().ViewProjection, identity);

		rig.SetViewportSize({640.0f, 480.0f});
		rig.Frame();
		REQUIRE(rig.HasRuntime());
		CHECK(rig.Runtime().Projection[1][1] == Catch::Approx(1.0f / std::tan(rig.world.GetRegistry().Read<CameraComponent>(rig.camera).PerspectiveFOV * 0.5f)));
	}
}

TEST_CASE("CameraRuntimeUpdateSystem re-resolves a target whose viewport entity died", "[camera]")
{
	Rig rig;
	rig.Frame();
	REQUIRE((rig.world.GetRegistry().Read<CameraTargetComponent>(rig.camera).TargetViewportEntity == rig.viewport));

	auto& reg = rig.world.GetRegistry();
	const entt::entity replacement = reg.create();
	reg.emplace<IDComponent>(replacement).Id = UUID(kViewportId + 5);
	reg.emplace<ViewportComponent>(replacement).Size = {300.0f, 300.0f};

	reg.destroy(rig.viewport);
	reg.patch<CameraTargetComponent>(rig.camera, [](CameraTargetComponent& t)
	                                 { t.TargetViewportUUID = UUID(kViewportId + 5); });
	rig.Frame();

	CHECK((reg.Read<CameraTargetComponent>(rig.camera).TargetViewportEntity == replacement));
	CHECK(rig.Runtime().Projection[0][0] == Catch::Approx(rig.Runtime().Projection[1][1]));
}
