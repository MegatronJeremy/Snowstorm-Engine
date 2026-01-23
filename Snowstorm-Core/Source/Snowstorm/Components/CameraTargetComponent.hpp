#pragma once

#include "Snowstorm/Utility/UUID.hpp"

#include <entt/entity/entity.hpp>

namespace Snowstorm
{
	struct CameraTargetComponent
	{
		UUID TargetViewportUUID{};
		entt::entity TargetViewportEntity = entt::null; // runtime cache
	};

	void RegisterCameraTargetComponent();
}
