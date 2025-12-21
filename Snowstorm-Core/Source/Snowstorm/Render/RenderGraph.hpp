#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Snowstorm
{
	class RenderGraph
	{
	public:
		struct Pass
		{
			std::string Name;

			// Target for dynamic rendering
			Ref<RenderTarget> Target;

			// Records commands for this pass
			std::function<void(CommandContext&)> Execute;
		};

		void Reset();

		// Ordered passes (minimal version)
		void AddPass(Pass pass);

		// Records passes into an already-begun frame command context.
		void Execute(CommandContext& ctx) const;

	private:
		std::vector<Pass> m_Passes;
	};
}