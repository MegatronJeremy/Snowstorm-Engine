#include "pch.h"
#include "CoreServices.hpp"

#include "Snowstorm/Service/ServiceManager.hpp"

#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/MeshLibrary.hpp"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	void RegisterCoreServices(ServiceManager& services)
	{
		// Job system first: it's the off-main-thread work pool the others may eventually submit to (async
		// asset loading), and it's device-independent so it can exist before the Vulkan-bound services.
		services.RegisterService<JobSystem>();

		// Device-bound, application-scoped subsystems. Registered after Renderer::Init so the Vulkan device
		// exists. Order among these is not significant (none tick, none depend on another at construction).
		services.RegisterService<RendererService>();
		services.RegisterService<ShaderLibrary>();
		services.RegisterService<MeshLibrary>();
	}
}
