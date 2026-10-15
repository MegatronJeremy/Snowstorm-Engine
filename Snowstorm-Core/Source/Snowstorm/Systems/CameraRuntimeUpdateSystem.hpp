#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class CameraRuntimeUpdateSystem final : public System
	{
	public:
		explicit CameraRuntimeUpdateSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		void DeclareAccess(SystemAccess& a) const override;

	private:
		void ResolveCameraTargets() const;
		void UpdateDirtyCameras() const;
	};
}
