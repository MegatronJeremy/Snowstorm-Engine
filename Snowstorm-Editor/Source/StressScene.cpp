#include "StressScene.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

#include <glm/glm.hpp>

#include <random>

namespace Snowstorm
{
	namespace
	{
		// Assemble a renderable: Transform + Mesh + Material + Visibility (Scene|Game). Mirrors the
		// SetupRenderable helper in EditorLayer::CreateDemoEntities.
		Entity MakeRenderable(World& world, const char* name, const AssetHandle mesh,
		                      const AssetHandle material, const glm::vec3& pos,
		                      const glm::vec3& rot, const glm::vec3& scale)
		{
			Entity e = world.CreateEntity(name);

			auto& tr = e.AddComponent<TransformComponent>();
			tr.Position = pos;
			tr.Rotation = rot;
			tr.Scale = scale;

			auto& mc = e.AddComponent<MeshComponent>();
			mc.MeshHandle = mesh;
			mc.MeshInstance.reset();

			auto& matc = e.AddComponent<MaterialComponent>();
			matc.Material = material;
			matc.MaterialInstance.reset();

			e.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
			return e;
		}

		void AddAlbedoOverride(Entity e, const AssetHandle texture)
		{
			auto& ov = e.AddOrReplaceComponent<MaterialOverridesComponent>();
			MaterialOverride o;
			o.Name = "AlbedoTexture";
			o.Type = MaterialOverrideType::Texture;
			o.Texture = texture;
			ov.Overrides.push_back(o);
		}
	}

	void BuildStressScene(World& world, const StressSceneParams& params)
	{
		auto& assets = world.GetSingleton<AssetManagerSingleton>();

		// Assets (idempotent imports).
		const AssetHandle cubeMesh = assets.Import("assets/meshes/cube.obj", AssetType::Mesh);
		const AssetHandle quadMesh = assets.Import("assets/meshes/quad.obj", AssetType::Mesh);
		const AssetHandle whiteMat = assets.Import("assets/materials/White.ssmat", AssetType::Material);

		const AssetHandle checkerTex = assets.Import("assets/textures/Checkerboard.png", AssetType::Texture);
		const AssetHandle sheetTex = assets.Import("assets/textures/RPGpack_sheet_2X.png", AssetType::Texture);

		// Deterministic PRNG so the scene is reproducible run-to-run (before/after benchmarks).
		std::mt19937 rng(params.Seed);
		auto frand = [&rng](const float lo, const float hi)
		{
			std::uniform_real_distribution<float> d(lo, hi);
			return d(rng);
		};

		int spawned = 0;

		// ---- Lights (mirror the demo scene's two directional lights) ----
		{
			auto a = world.CreateEntity("Stress Light A");
			auto& la = a.AddComponent<DirectionalLightComponent>();
			la.Direction = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));
			la.Color = glm::vec3(1.0f, 0.95f, 0.85f);
			la.Intensity = 1.0f;
			a.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;

			auto b = world.CreateEntity("Stress Light B");
			auto& lb = b.AddComponent<DirectionalLightComponent>();
			lb.Direction = glm::normalize(glm::vec3(-0.7f, -0.6f, -0.4f));
			lb.Color = glm::vec3(0.7f, 0.8f, 1.0f);
			lb.Intensity = 0.7f;
			b.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
		}

		// ---- High-frequency albedo field: a grid of textured tiles laid flat (XZ plane) ----
		const float half = static_cast<float>(params.GridDim - 1) * 0.5f * params.GridSpacing;
		for (int z = 0; z < params.GridDim; ++z)
		{
			for (int x = 0; x < params.GridDim; ++x)
			{
				const glm::vec3 pos = {static_cast<float>(x) * params.GridSpacing - half,
				                       0.0f,
				                       static_cast<float>(z) * params.GridSpacing - half};
				// Lay the quad flat: rotate -90deg about X so it faces up.
				Entity e = MakeRenderable(world, "HF Tile", quadMesh, whiteMat, pos,
				                          {glm::radians(-90.0f), 0.0f, 0.0f},
				                          {params.TileScale, params.TileScale, params.TileScale});
				// Alternate textures in a checker pattern to maximize high-frequency content.
				AddAlbedoOverride(e, ((x + z) & 1) ? checkerTex : sheetTex);
				++spawned;
			}
		}

		// ---- Thin-geometry forest: many sub-pixel-thin pillars scattered over the field ----
		for (int i = 0; i < params.ThinCount; ++i)
		{
			const glm::vec3 pos = {frand(-half, half), params.ThinHeight * 0.5f, frand(-half, half)};
			Entity e = MakeRenderable(world, "Thin Pillar", cubeMesh, whiteMat, pos,
			                          {0.0f, frand(0.0f, glm::radians(360.0f)), 0.0f},
			                          {params.ThinThickness, params.ThinHeight, params.ThinThickness});
			AddAlbedoOverride(e, checkerTex);
			++spawned;
		}

		// ---- Disocclusion layer: rotating occluders that reveal/hide the field behind them ----
		for (int i = 0; i < params.OccluderCount; ++i)
		{
			const glm::vec3 pos = {frand(-half * 0.7f, half * 0.7f),
			                       frand(1.5f, 4.0f),
			                       frand(-half * 0.7f, half * 0.7f)};
			Entity e = MakeRenderable(world, "Occluder", cubeMesh, whiteMat, pos,
			                          {frand(0.0f, 1.0f), frand(0.0f, 1.0f), frand(0.0f, 1.0f)},
			                          {frand(0.8f, 2.0f), frand(0.8f, 2.0f), frand(0.8f, 2.0f)});
			AddAlbedoOverride(e, sheetTex);

			auto& rot = e.AddComponent<RotatorComponent>();
			rot.Axis = glm::normalize(glm::vec3(frand(-1.0f, 1.0f), 1.0f, frand(-1.0f, 1.0f)));
			rot.SpeedDegPerSec = frand(15.0f, 60.0f);
			++spawned;
		}

		// ---- Heavy data-parallel ECS field: Transform+Rotator-only entities (#85 demonstrator) ----
		// No mesh/material/visibility -> these never touch the draw path; they exist purely to give
		// RotatorSystem a large, pure per-entity workload so the serial-vs-parallel win is measurable.
		int rotators = 0;
		for (int i = 0; i < params.RotatorCount; ++i)
		{
			Entity e = world.CreateEntity("Rotator");

			auto& tr = e.AddComponent<TransformComponent>();
			tr.Position = {frand(-half, half), frand(0.0f, 8.0f), frand(-half, half)};
			tr.Rotation = {frand(0.0f, glm::radians(360.0f)), frand(0.0f, glm::radians(360.0f)), frand(0.0f, glm::radians(360.0f))};

			auto& rot = e.AddComponent<RotatorComponent>();
			rot.Axis = glm::normalize(glm::vec3(frand(-1.0f, 1.0f), 1.0f, frand(-1.0f, 1.0f)));
			rot.SpeedDegPerSec = frand(15.0f, 90.0f);
			++rotators;
		}

		SS_CORE_INFO("Stress scene built: {} renderables (grid {}x{}, {} pillars, {} occluders) + {} bare rotators",
		             spawned, params.GridDim, params.GridDim, params.ThinCount, params.OccluderCount, rotators);
	}
}
