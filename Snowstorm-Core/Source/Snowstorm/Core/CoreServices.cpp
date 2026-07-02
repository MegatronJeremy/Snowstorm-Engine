#include "pch.h"
#include "CoreServices.hpp"

#include "Snowstorm/Service/ServiceManager.hpp"

#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/MeshLibrary.hpp"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	void RegisterCoreServices(ServiceManager& services)
	{
		// Device-bound, application-scoped subsystems. Registered after Renderer::Init so the Vulkan device
		// exists. Order is not significant (none tick, none depend on another at construction).
		services.RegisterService<RendererService>();
		services.RegisterService<ShaderLibrary>();
		services.RegisterService<MeshLibrary>();
	}
}
