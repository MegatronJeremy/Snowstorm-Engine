#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Service/Service.hpp"

namespace Snowstorm
{
	class ImGuiService final : public Service
	{
	public:
		ImGuiService();
		~ImGuiService() override;

		void OnUpdate(Timestep ts) override;
		void PostUpdate(Timestep ts) override;
	};
}
