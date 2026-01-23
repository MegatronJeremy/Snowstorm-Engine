#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorNotificationSystem : public System
	{
	public:
		using System::System;
		void Execute(Timestep ts) override;
	};
}
