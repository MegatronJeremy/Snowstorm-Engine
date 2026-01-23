#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class VisibilitySystem final : public System
	{
	public:
		using System::System;
		void Execute(Timestep ts) override;

	private:
		[[nodiscard]] bool IsVisibilityDirtyThisFrame() const;
	};
}
