#include "CoreSystems.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/World/World.hpp"

#include "Snowstorm/Lighting/EnvironmentSystem.hpp"
#include "Snowstorm/Lighting/LightingSystem.hpp"
#include "Snowstorm/Systems/AssetLoadSystem.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/CameraJitterSystem.hpp"
#include "Snowstorm/Systems/CameraPathSystem.hpp"
#include "Snowstorm/Systems/CameraRuntimeUpdateSystem.hpp"
#include "Snowstorm/Systems/MaterialResolveSystem.hpp"
#include "Snowstorm/Systems/MeshResolveSystem.hpp"
#include "Snowstorm/Systems/PrevTransformSnapshotSystem.hpp"
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
		// After the controller so the scripted benchmark path overrides free-fly input when camera.path is on.
		sm.RegisterSystem<CameraPathSystem>(SystemPhase::Logic);
		sm.RegisterSystem<RotatorSystem>(SystemPhase::Logic);

		sm.RegisterSystem<ShaderReloadSystem>(SystemPhase::AssetSync);
		// Pump worker-completed async loads (GPU finalize) before the Resolve phase consumes them.
		sm.RegisterSystem<AssetLoadSystem>(SystemPhase::AssetSync);

		sm.RegisterSystem<CameraRuntimeUpdateSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MeshResolveSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MaterialResolveSystem>(SystemPhase::Resolve);

		sm.RegisterSystem<EnvironmentSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<LightingSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<VisibilitySystem>(SystemPhase::PreRender);
		// Temporal jitter (#44): fill each camera's JitteredViewProjection every frame. After the runtime
		// update (Resolve) so Projection/View exist; reads the unjittered VP, writes a jittered copy.
		sm.RegisterSystem<CameraJitterSystem>(SystemPhase::PreRender);

		sm.RegisterSystem<RenderSystem>(SystemPhase::Render);

		// End-of-frame: snapshot this frame's transforms into the *Prev* slots for next frame's motion
		// vectors (#44). Registered AFTER RenderSystem so it captures the values the frame just rendered.
		sm.RegisterSystem<PrevTransformSnapshotSystem>(SystemPhase::Render);
	}
}
