#include "pch.h"
#include "CoreSystems.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/World/World.hpp"

#include "Snowstorm/Lighting/EnvironmentSystem.hpp"
#include "Snowstorm/Lighting/LightingSystem.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/CameraRuntimeUpdateSystem.hpp"
#include "Snowstorm/Systems/MaterialResolveSystem.hpp"
#include "Snowstorm/Systems/MeshResolveSystem.hpp"
#include "Snowstorm/Systems/RenderSystem.hpp"
#include "Snowstorm/Systems/RotatorSystem.hpp"
#include "Snowstorm/Systems/RuntimeInitSystem.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/Systems/ShaderReloadSystem.hpp"
#include "Snowstorm/Systems/VisibilitySystem.hpp"

namespace Snowstorm
{
	void RegisterCoreSystems(World& world)
	{
		auto& sm = world.GetSystemManager();

		sm.RegisterSystem<RuntimeInitSystem>(SystemPhase::Init);

		sm.RegisterSystem<ScriptSystem>(SystemPhase::Logic);
		sm.RegisterSystem<CameraControllerSystem>(SystemPhase::Logic);
		sm.RegisterSystem<RotatorSystem>(SystemPhase::Logic);

		sm.RegisterSystem<ShaderReloadSystem>(SystemPhase::AssetSync);

		sm.RegisterSystem<CameraRuntimeUpdateSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MeshResolveSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MaterialResolveSystem>(SystemPhase::Resolve);

		sm.RegisterSystem<EnvironmentSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<LightingSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<VisibilitySystem>(SystemPhase::PreRender);

		sm.RegisterSystem<RenderSystem>(SystemPhase::Render);
	}
}
