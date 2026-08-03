#include "TlasBuildSystem.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Systems/ReflectionGeometrySingleton.hpp"
#include "Snowstorm/Systems/TlasInstanceMapSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include "Platform/Vulkan/VulkanBindlessManager.hpp"
#include "Platform/Vulkan/VulkanTlas.hpp"

namespace Snowstorm
{
	bool TlasBuildSystem::IsSceneDirtyThisFrame() const
	{
		// The TLAS instances are exactly the (Transform + Mesh) entities. It needs a rebuild only when the
		// instance SET changes (a mesh/transform added or removed, a mesh resolved) or an instance's
		// PLACEMENT changes (the transform of a MESH entity moved).
		//
		// Add/remove are one-shot events (spawn/despawn) — cheap to over-trigger, so left unfiltered.
		if (!ChangedView<MeshComponent>().empty()) // mesh resolved / swapped
			return true;
		if (!InitView<MeshComponent>().empty() || !InitView<TransformComponent>().empty())
			return true;
		if (!FiniView<MeshComponent>().empty() || !FiniView<TransformComponent>().empty())
			return true;

		// A whole-entity DESTROY (editor delete, despawn) is tracked separately from component removal —
		// FiniView/RemovedView only sees explicit Remove<T>(), not registry.destroy(), so a deleted mesh
		// would otherwise stay in the TLAS and keep casting an RT shadow/reflection/GI ghost after it's gone.
		// Destroys are rare one-shot events, so rebuilding on any destroy is cheap (matches the add/remove
		// "over-trigger is fine" stance above).
		if (m_World->GetRegistry().AnyDestroyedThisFrame())
			return true;

		// A MaterialComponent change must also rebuild: the geometry table caches each instance's material
		// constants (albedo texture index, base color) for the RT reflection/GI shade. Materials resolve
		// ASYNC and INDEPENDENTLY of meshes (MaterialResolveSystem sets MaterialInstance only once the
		// pipeline's shader has compiled), so on a cold cache a mesh resolves + builds the table BEFORE its
		// material is ready — the record captures the white/BaseColor fallback (see the try_get_const path
		// below) and, without this check, never refreshes when the material lands, leaving GI/reflections lit
		// with wrong albedo until something else re-dirties the scene (the "toggle GI/refl off+on fixes it"
		// bug). Optimized shaders (slower cold compile) made this window reliably straddle the first build.
		if (!ChangedView<MaterialComponent>().empty())
			return true;

		// Placement change is the PER-FRAME hot path: only a changed transform that belongs to a mesh entity
		// moves an instance. This filters out the camera — whose transform CameraControllerSystem rewrites
		// every frame you move — so free-flying the view does NOT rebuild the TLAS (the camera isn't an
		// instance; moving it changes no geometry).
		const auto& reg = m_World->GetRegistry();
		for (const entt::entity e : ChangedView<TransformComponent>())
		{
			if (reg.all_of<MeshComponent>(e))
			{
				return true;
			}
		}
		return false;
	}

	void TlasBuildSystem::Execute(Timestep)
	{
		// Only maintain the TLAS while something actually samples it — RT shadows, RTAO, or RT reflections. In
		// every other mode building it is pure waste. Each helper folds in the device-support check (false on a
		// non-RT GPU). Track the state so the OFF->ON transition can force a rebuild below.
		// The per-instance geometry/material table is needed by any effect that SHADES a ray hit — RT
		// reflections and RT GI both resolve hits through it. (Shadows/AO are occupancy-only, no table.)
		const bool geoTableNeeded = CVars::ReflectionsRTActive() || CVars::GIRTActive();
		const bool rtActive = CVars::ShadowsRTActive() || CVars::AoRTActive() || geoTableNeeded;
		const bool justEnabled = rtActive && !m_WasRTActive;
		// Reflections/GI need the per-instance geometry table; shadows/AO don't. If the TLAS is already being
		// maintained for shadows/AO and reflections/GI are THEN turned on, geoTableNeeded flips false->true
		// without rtActive changing — so justEnabled stays false and a static scene's dirty-check skips the
		// rebuild, leaving TableAddress 0 and the reflection/GI trace falling back to sky (the "toggles don't
		// work" bug). Force a rebuild on that sub-transition too.
		const bool geoTableJustNeeded = geoTableNeeded && !m_WasGeoTableNeeded;
		m_WasRTActive = rtActive;
		m_WasGeoTableNeeded = geoTableNeeded;
		if (!rtActive)
		{
			return;
		}

		// Rebuild when the scene changed OR RT just turned on OR the geometry table just became needed (the
		// scene's per-frame dirty flags were consumed on prior frames, so a plain dirty-check would miss both
		// enable edges).
		if (m_BuiltOnce && !justEnabled && !geoTableJustNeeded && !IsSceneDirtyThisFrame())
		{
			return;
		}

		auto& reg = m_World->GetRegistry();

		// Gather one instance per (Transform + resolved Mesh) entity, building each mesh's BLAS lazily.
		// The entity of each emitted instance is recorded in lockstep (same order, same skips) so the RT
		// picking path can map a committed instance index back to its entity. VulkanTlas stamps
		// instanceCustomIndex = the instance's position in this vector, which is exactly instanceEntities'
		// index — so instanceEntities[CommittedInstanceID()] resolves the hit.
		std::vector<TLASInstance> instances;
		std::vector<entt::entity> instanceEntities;
		// RT reflections (#118): a parallel per-instance geometry/material table so a reflected hit resolves to
		// a shadeable surface. Only gathered when reflections are active (dead weight otherwise). Filled in
		// lockstep with `instances`, so record[i] describes the instance the GPU stamps instanceCustomIndex = i.
		std::vector<GeometryRecord> geoRecords;
		for (auto view = reg.view<TransformComponent, MeshComponent>(); const entt::entity e : view)
		{
			const auto& mc = reg.Read<MeshComponent>(e);
			if (!mc.MeshInstance) // mesh not resolved yet (async load in flight)
			{
				continue;
			}

			const Ref<BLAS>& blas = mc.MeshInstance->GetOrBuildBLAS();
			if (!blas)
			{
				continue;
			}

			const auto& tc = reg.Read<TransformComponent>(e);
			const glm::mat4 model = tc.GetTransformMatrix();
			instances.push_back({model, blas->GetDeviceAddress()});
			instanceEntities.push_back(e);

			if (geoTableNeeded)
			{
				GeometryRecord rec{};
				rec.VertexAddress = mc.MeshInstance->GetVertexBuffer()->GetGPUAddress();
				rec.IndexAddress = mc.MeshInstance->GetIndexBuffer()->GetGPUAddress();
				rec.Model = model;
				// Material may not be resolved yet (async) — a null record still shades as BaseColor white; the
				// table stays index-aligned regardless, so a missing material never desyncs the mapping.
				if (const auto* matc = reg.try_get_const<MaterialComponent>(e); matc && matc->MaterialInstance)
				{
					const Material::Constants& c = matc->MaterialInstance->GetConstants();
					rec.AlbedoTextureIndex = c.AlbedoTextureIndex;
					rec.BaseColor = c.BaseColor;
				}
				geoRecords.push_back(rec);
			}
		}

		// Publish the index->entity table for RT picking (consumed by the editor). Rebuilt every TLAS build
		// so it never drifts from what the GPU traces.
		SingletonView<TlasInstanceMapSingleton>().Instances = std::move(instanceEntities);

		// Publish the reflection geometry table (consumed by RendererService -> DefaultLit). Grow the GPU
		// buffer when the instance count outgrows it (device-address Storage so the shader can RawBufferLoad
		// records); address 0 when reflections are off so the shader falls back to the sky cube.
		auto& reflGeo = SingletonView<ReflectionGeometrySingleton>();
		if (geoTableNeeded && !geoRecords.empty())
		{
			const uint32_t needed = static_cast<uint32_t>(geoRecords.size());
			if (!reflGeo.Table || reflGeo.Capacity < needed)
			{
				reflGeo.Capacity = needed;
				reflGeo.Table = Buffer::Create(static_cast<size_t>(needed) * sizeof(GeometryRecord),
				                               BufferUsage::Storage, nullptr, true, "ReflectionGeometryTable");
			}
			reflGeo.Table->SetData(geoRecords.data(), needed * sizeof(GeometryRecord), 0);
			// Publish the address EVERY time the table is populated, not only when the buffer is (re)created.
			// The buffer survives an RT-off cycle (it's cached, not freed), while the else-branch below zeroes
			// TableAddress when the table isn't needed. So re-enabling reflections/GI after a shadows/AO-only
			// spell reuses the existing buffer, skips the creation branch, and — if the address were set only
			// there — would leave TableAddress 0 forever (GI/reflections dead until restart). Set it here.
			reflGeo.TableAddress = reflGeo.Table->GetGPUAddress();
		}
		else
		{
			reflGeo.TableAddress = 0; // no table this frame -> shader uses the sky-cube fallback
		}

		if (!m_TLAS)
		{
			m_TLAS = TLAS::Create("SceneTLAS");
		}
		m_TLAS->Build(instances);
		m_BuiltOnce = true;

		// Point the bindless TLAS slot at the freshly built AS so ray-query shaders trace this scene.
		const auto vkTlas = std::static_pointer_cast<VulkanTlas>(m_TLAS);
		VulkanBindlessManager::Get().WriteAccelerationStructure(vkTlas->GetHandle());

		// Log only when the instance count changes (streaming settle, scene switch) — not every transform
		// tweak — so dragging an object in the editor (a rebuild per frame) doesn't spam the log.
		const uint32_t count = m_TLAS->GetInstanceCount();
		if (count != m_LastLoggedCount)
		{
			SS_CORE_INFO("TLAS rebuilt: {} instance(s).", count);
			m_LastLoggedCount = count;
		}
	}
}
