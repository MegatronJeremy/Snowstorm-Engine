#include "PrimaryCameraSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"

namespace Snowstorm
{
	void PrimaryCameraSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();

		// Find the authored (non-DoNotSerialize) Primary cameras. When more than one is Primary, keep the one
		// that CHANGED this frame (the user's fresh intent — a just-set "Set as Primary", a deserialized flag),
		// else the first seen, and demote the rest. Converges in one frame: the demote patch re-marks Changed,
		// but next frame only one Primary remains, so the count is 1 and this is a no-op.
		entt::entity winner = entt::null;
		int primaryCount = 0;
		const auto changed = ChangedView<CameraComponent>();
		for (const auto view = reg.view<CameraComponent>(); const entt::entity e : view)
		{
			if (reg.any_of<DoNotSerializeComponent>(e) || !reg.Read<CameraComponent>(e).Primary)
			{
				continue;
			}
			++primaryCount;
			if (winner == entt::null || changed.contains(e))
			{
				winner = e;
			}
		}

		if (primaryCount <= 1)
		{
			return;
		}

		for (const auto view = reg.view<CameraComponent>(); const entt::entity e : view)
		{
			if (e != winner && !reg.any_of<DoNotSerializeComponent>(e) && reg.Read<CameraComponent>(e).Primary)
			{
				reg.patch<CameraComponent>(e, [](CameraComponent& c)
				                           { c.Primary = false; });
			}
		}
	}
}
