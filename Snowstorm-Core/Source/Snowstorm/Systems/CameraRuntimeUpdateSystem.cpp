#include "CameraRuntimeUpdateSystem.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>

namespace Snowstorm
{
	namespace
	{
		bool HasValidViewportSize(const ViewportComponent& vp)
		{
			return vp.Size.x >= 1.0f && vp.Size.y >= 1.0f;
		}

		float ComputeAspect(const CameraComponent& cam, const ViewportComponent& vp)
		{
			if (cam.FixedAspectRatio && cam.AspectRatio > 0.0001f)
				return cam.AspectRatio;

			const float w = vp.Size.x;
			const float h = vp.Size.y;
			return (h > 0.0001f) ? (w / h) : (16.0f / 9.0f);
		}

		// NOTE:
		// Your engine’s clip space conventions matter (Vulkan vs D3D/OpenGL).
		// This uses glm defaults. If you use Vulkan (0..1 Z) you likely want:
		// - glm::perspectiveRH_ZO
		// - and possibly flip Y (proj[1][1] *= -1)
		glm::mat4 BuildProjection(const CameraComponent& cam, float aspect)
		{
			if (cam.Projection == CameraComponent::ProjectionType::Perspective)
			{
				return glm::perspectiveRH_ZO(cam.PerspectiveFOV, aspect, cam.PerspectiveNear, cam.PerspectiveFar);
			}

			// OrthographicSize is “height” in world units (common convention)
			const float halfH = cam.OrthographicSize * 0.5f;
			const float halfW = halfH * aspect;

			return glm::ortho(-halfW, halfW, -halfH, halfH, cam.OrthographicNear, cam.OrthographicFar);
		}
	}

	void CameraRuntimeUpdateSystem::Execute(Timestep /*ts*/)
	{
		ResolveCameraTargets();
		UpdateDirtyCameras();
	}

	void CameraRuntimeUpdateSystem::ResolveCameraTargets() const
	{
		auto& reg = m_World->GetRegistry();

		// Build UUID -> viewport entity lookup (only entities that *are* viewports)
		std::unordered_map<UUID, entt::entity> viewportByUUID;

		const auto viewportIdView = reg.view<IDComponent, ViewportComponent>();
		for (const auto e : viewportIdView)
		{
			const auto& id = viewportIdView.get<IDComponent>(e);
			viewportByUUID[id.Id] = e;
		}

		const auto camTargetView = reg.ChangedView<CameraTargetComponent>();
		for (const auto e : camTargetView)
		{
			auto& ct = reg.Write<CameraTargetComponent>(e);

			if (ct.TargetViewportEntity != entt::null)
			{
				// Keep the cache valid if the entity died or lost viewport component
				if (!reg.valid(ct.TargetViewportEntity) || !reg.any_of<ViewportComponent>(ct.TargetViewportEntity))
				{
					ct.TargetViewportEntity = entt::null;
				}
				else
				{
					continue;
				}
			}

			if (ct.TargetViewportUUID.Value() == 0)
			{
				continue;
			}

			if (const auto it = viewportByUUID.find(ct.TargetViewportUUID); it != viewportByUUID.end())
			{
				ct.TargetViewportEntity = it->second;
			}
		}
	}

	void CameraRuntimeUpdateSystem::UpdateDirtyCameras() const
	{
		auto& reg = m_World->GetRegistry();

		// Viewports that changed size this frame
		const std::unordered_set<entt::entity> changedViewports = reg.ChangedView<ViewportComponent>();

		// Cameras that changed config/transform/target mapping this frame
		const std::unordered_set<entt::entity> changedCamsA = reg.ChangedView<CameraComponent>();
		const std::unordered_set<entt::entity> changedCamsB = reg.ChangedView<TransformComponent>();
		const std::unordered_set<entt::entity> changedCamsC = reg.ChangedView<CameraTargetComponent>();

		// Cameras newly created/added (need runtime init)
		const std::unordered_set<entt::entity> addedCams = reg.AddedView<CameraComponent, TransformComponent>();

		// Iterate all cameras; update only if dirty
		const auto camView = reg.view<TransformComponent, CameraComponent, CameraTargetComponent>();

		for (const auto camE : camView)
		{
			const bool dirtyDirect =
			addedCams.contains(camE) ||
			changedCamsA.contains(camE) ||
			changedCamsB.contains(camE) ||
			changedCamsC.contains(camE);

			const auto& ct = camView.get<CameraTargetComponent>(camE);
			if (ct.TargetViewportEntity == entt::null)
			{
				continue; // no target yet; ResolveCameraTargets will fill it when possible
			}

			// If the viewport this camera targets resized, camera must update (aspect/proj)
			const bool dirtyViewport = changedViewports.contains(ct.TargetViewportEntity);

			if (!dirtyDirect && !dirtyViewport)
			{
				continue;
			}

			// Ensure runtime component exists
			if (!reg.any_of<CameraRuntimeComponent>(camE))
			{
				reg.emplace<CameraRuntimeComponent>(camE);
			}

			const auto& tr = camView.get<TransformComponent>(camE);
			const auto& cam = camView.get<CameraComponent>(camE);

			const auto& vp = reg.Read<ViewportComponent>(ct.TargetViewportEntity);
			if (!HasValidViewportSize(vp))
			{
				continue;
			}

			const float aspect = ComputeAspect(cam, vp);

			auto& rt = reg.Write<CameraRuntimeComponent>(camE);

			// Preserve previous VP for motion vectors / temporal stuff
			rt.PrevViewProjection = rt.ViewProjection;

			// TransformComponent in your code behaves like a matrix already
			const glm::mat4 world = tr;
			rt.View = glm::inverse(world);
			rt.Projection = BuildProjection(cam, aspect);
			rt.ViewProjection = rt.Projection * rt.View;

			// Update frustum from VP
			rt.frustum = Frustum::FromViewProjection(rt.ViewProjection);
		}
	}
}
