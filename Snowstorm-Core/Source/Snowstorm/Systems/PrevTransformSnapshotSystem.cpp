#include "PrevTransformSnapshotSystem.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/PrevTransformComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void PrevTransformSnapshotSystem::Execute(Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();

		// Meshes: snapshot this frame's world matrix into PrevModel. Ensure<> creates the component the
		// first frame an object exists (PrevModel starts == current -> zero velocity, correct); thereafter
		// we write through the untracked get<> so we don't mark it Changed every single frame.
		const auto meshView = reg.view<TransformComponent, MeshComponent>();
		for (const auto e : meshView)
		{
			auto& prev = reg.Ensure<PrevTransformComponent>(e);
			prev.PrevModel = meshView.get<TransformComponent>(e).GetTransformMatrix();
		}

		// Cameras: snapshot this frame's VP so a moving-then-stopped camera reports zero motion next frame
		// (CameraRuntimeUpdateSystem only refreshes PrevViewProjection when the camera is dirty, which
		// leaves it stale for a static camera). Untracked write — nothing consumes CameraRuntime via
		// ChangedView on this field.
		const auto camView = reg.view<CameraRuntimeComponent>();
		for (const auto e : camView)
		{
			auto& rt = reg.get<CameraRuntimeComponent>(e);
			rt.PrevViewProjection = rt.ViewProjection;
		}
	}
}
