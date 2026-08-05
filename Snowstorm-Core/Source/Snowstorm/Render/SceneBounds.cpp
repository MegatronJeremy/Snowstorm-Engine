#include "SceneBounds.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Math/CameraFraming.hpp"
#include "Snowstorm/Render/Mesh.hpp"

#include <limits>

namespace Snowstorm
{
	namespace
	{
		// World-space AABB of one entity's mesh. Uses the ALREADY-RESOLVED MeshInstance (populated
		// asynchronously by MeshResolveSystem) — it must NOT call the blocking AssetManager::GetMesh:
		// this runs every frame from ComputeWorldRenderableAABB (shadow fitting), and a synchronous
		// GetMesh here would load every scene mesh's GPU data on the first shadow frame, blocking the
		// main thread for ~1s (the whole point of async streaming is defeated otherwise). An entity whose
		// mesh hasn't streamed in yet is simply skipped; it joins the bounds the frame its mesh arrives
		// (bounds are recomputed each frame).
		bool EntityAABB(World& world, const entt::entity e, AABB& out)
		{
			auto& reg = world.GetRegistry();
			if (!reg.all_of<MeshComponent, TransformComponent>(e))
			{
				return false;
			}

			const Ref<Mesh>& mesh = reg.Read<MeshComponent>(e).MeshInstance;
			if (!mesh)
			{
				return false; // not resolved yet — skip until it streams in
			}

			out = TransformAABB(mesh->GetBounds().Box, reg.Read<TransformComponent>(e).GetTransformMatrix());
			return true;
		}
	}

	bool ComputeWorldRenderableAABB(World& world, AABB& out)
	{
		auto& reg = world.GetRegistry();

		AABB acc{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
		bool any = false;

		for (const auto view = reg.view<MeshComponent, TransformComponent>(); const entt::entity e : view)
		{
			AABB box;
			if (!EntityAABB(world, e, box))
			{
				continue;
			}
			acc.Min = glm::min(acc.Min, box.Min);
			acc.Max = glm::max(acc.Max, box.Max);
			any = true;
		}

		if (any)
		{
			out = acc;
		}
		return any;
	}

	bool ComputeEntityRenderableAABB(World& world, const entt::entity entity, AABB& out)
	{
		return EntityAABB(world, entity, out);
	}

	void FramePrimaryCameraOnAABB(World& world, const AABB& bounds, const bool adjustClipPlanes)
	{
		// Frame the EDITOR's Scene-view camera (the fly-camera), identified by DoNotSerializeComponent — NOT
		// "any Primary camera". Focus (F/Ctrl+F) and the initial whole-scene fit are editor-view actions; a
		// scene can now author its own Primary gameplay camera (#147), and framing THAT would move the game
		// camera while the editor view sits still (so focus appears to do nothing). Same identity filter as
		// EditorLayer::FindEditorCamera.
		auto& reg = world.GetRegistry();
		for (const auto view = reg.view<CameraComponent, TransformComponent, DoNotSerializeComponent>(); const entt::entity e : view)
		{
			// The editor Scene-view camera is identified by DoNotSerialize alone (the only such camera), NOT by
			// Primary — matching EditorLayer::FindEditorCamera. Framing must work even when the user has cleared
			// the editor camera's Primary flag (Primary is the authored-gameplay-camera concept, user-toggleable);
			// gating on it here would make F/Ctrl+F silently do nothing in that state.
			auto& cam = reg.Write<CameraComponent>(e);

			const FramingPose pose = ComputeFramingPose(bounds, cam.PerspectiveFOV);

			auto& tr = reg.Write<TransformComponent>(e);
			tr.Position = pose.Position;
			tr.Rotation = glm::vec3(pose.Pitch, pose.Yaw, 0.0f);

			// Interactive focus only moves the camera; it must NOT reshape the frustum, or focusing a
			// small object would shrink the far plane and clip the scene when you fly back out. Only the
			// initial whole-scene framing (bake) fits the clip planes.
			if (adjustClipPlanes)
			{
				cam.PerspectiveNear = pose.Near;
				cam.PerspectiveFar = pose.Far;
			}

			// The controller eases rotation toward a target pitch/yaw; sync it so the focus snap isn't
			// immediately smoothed back to the old orientation.
			if (reg.all_of<CameraControllerRuntimeComponent>(e))
			{
				auto& rt = reg.Write<CameraControllerRuntimeComponent>(e);
				rt.TargetPitch = pose.Pitch;
				rt.TargetYaw = pose.Yaw;
			}
			break;
		}
	}

	bool FrameCameraOnEntity(World& world, const entt::entity entity)
	{
		AABB bounds;
		if (!EntityAABB(world, entity, bounds))
		{
			return false;
		}
		FramePrimaryCameraOnAABB(world, bounds);
		return true;
	}

	entt::entity ResolveViewportCamera(const TrackedRegistry& reg, const entt::entity viewport)
	{
		if (viewport == entt::null)
		{
			return entt::null;
		}

		// Two-pass over cameras targeting this viewport: a Primary wins outright; otherwise remember the first
		// candidate and return it once we've confirmed no Primary exists. Single loop with a saved fallback so
		// iteration order is the only tiebreak (deterministic), matching what the three former hand-rolled
		// finders each did — now in one place so render / gizmo / controller can't disagree.
		entt::entity fallback = entt::null;
		for (const auto view = reg.view<const CameraComponent, const CameraTargetComponent>();
		     const entt::entity e : view)
		{
			if (reg.Read<CameraTargetComponent>(e).TargetViewportEntity != viewport)
			{
				continue;
			}
			if (reg.Read<CameraComponent>(e).Primary)
			{
				return e; // Primary targeting this viewport — the definitive answer
			}
			if (fallback == entt::null)
			{
				fallback = e; // first non-Primary candidate; used only if no Primary shows up
			}
		}
		return fallback;
	}
}
