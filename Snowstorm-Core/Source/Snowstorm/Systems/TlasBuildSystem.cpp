#include "TlasBuildSystem.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
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
		const bool reflectionsActive = CVars::ReflectionsRTActive();
		const bool rtActive = CVars::ShadowsRTActive() || CVars::AoRTActive() || reflectionsActive;
		const bool justEnabled = rtActive && !m_WasRTActive;
		m_WasRTActive = rtActive;
		if (!rtActive)
		{
			return;
		}

		// Rebuild when the scene changed OR RT shadows just turned on (the scene's per-frame dirty flags were
		// consumed while RT was off, so a plain dirty-check would miss the need to build the first TLAS).
		if (m_BuiltOnce && !justEnabled && !IsSceneDirtyThisFrame())
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

			if (reflectionsActive)
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
		if (reflectionsActive && !geoRecords.empty())
		{
			const uint32_t needed = static_cast<uint32_t>(geoRecords.size());
			if (!reflGeo.Table || reflGeo.Capacity < needed)
			{
				reflGeo.Capacity = needed;
				reflGeo.Table = Buffer::Create(static_cast<size_t>(needed) * sizeof(GeometryRecord),
				                               BufferUsage::Storage, nullptr, true, "ReflectionGeometryTable");
				reflGeo.TableAddress = reflGeo.Table->GetGPUAddress();
			}
			reflGeo.Table->SetData(geoRecords.data(), needed * sizeof(GeometryRecord), 0);
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
