#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	struct CameraComponent;
	struct CameraControllerComponent;
	struct CameraControllerRuntimeComponent;
	struct TransformComponent;
	struct ViewportInteractionComponent;

	class CameraControllerSystem final : public System
	{
	public:
		explicit CameraControllerSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		void DeclareAccess(SystemAccess& a) const override;

	private:
		void Execute2D(Timestep ts, CameraComponent& cam, TransformComponent& tr, const CameraControllerComponent& ctrl,
		               CameraControllerRuntimeComponent& rtState, const ViewportInteractionComponent& vpI,
		               entt::entity viewport);
	};
}
