#include "TlasBuildSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Renderer.hpp"
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
		// Only maintain the TLAS + its per-instance geometry table while some RT effect actually samples them;
		// in every other mode building them is pure waste. Each helper folds in the device-support check
		// (false on a non-RT GPU). Track the state so the OFF->ON transition can force a rebuild below.
		// The table is needed by EVERY RT effect: reflections/GI shade a ray hit through it, and shadows/AO
		// alpha-test cutout (glTF MASK) geometry through it (Inc 2: masked instances are FORCE_NON_OPAQUE and
		// the traversal samples the albedo alpha at the hit UV). So table-need is exactly rt-active.
		const bool rtActive = CVars::ShadowsRTActive() || CVars::AoRTActive() || CVars::ReflectionsRTActive() ||
		                      CVars::GIRTActive() || CVars::PathTraceActive();
		const bool justEnabled = rtActive && !m_WasRTActive;
		m_WasRTActive = rtActive;
		if (!rtActive)
		{
			return;
		}

		// Toggling render.omm swaps which BLAS each cutout instance uses (OMM vs any-hit); force a rebuild on
		// the edge so the A/B / safety switch takes effect on a static scene.
		const bool ommEnabled = CVars::OmmEnabled.Get();
		const bool ommToggled = m_BuiltOnce && ommEnabled != m_LastOmmEnabled;
		m_LastOmmEnabled = ommEnabled;

		// While textures are still streaming, keep rebuilding: a cutout instance whose albedo isn't resident yet
		// uses the any-hit fallback BLAS this frame (correct but unoptimized) and its OMM must bake once the real
		// pixels arrive. Nothing else marks the scene dirty on a texture-only completion, so gate on the async
		// load count. m_PrevPendingLoads carries one extra frame past the drain so the just-resident albedo
		// triggers the final rebuild that bakes its OMM.
		auto& assets = SingletonView<AssetManagerSingleton>();
		const uint32_t pendingLoads = assets.PendingLoadCount();
		const bool streaming = pendingLoads > 0 || m_PrevPendingLoads > 0;
		m_PrevPendingLoads = pendingLoads;

		// Rebuild when the scene changed OR RT just turned on OR render.omm toggled OR assets are still streaming
		// (the scene's per-frame dirty flags were consumed on prior frames, so a plain dirty-check would miss
		// those edges).
		if (m_BuiltOnce && !justEnabled && !ommToggled && !streaming && !IsSceneDirtyThisFrame())
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
		// Per-instance geometry/material table (#118), gathered whenever any RT effect is active. A reflected/
		// GI hit resolves to a shadeable surface through it, and shadow/AO any-hit rays alpha-test cutout
		// geometry through it (Inc 2). Filled in lockstep with `instances`, so record[i] describes the
		// instance the GPU stamps instanceCustomIndex = i.
		std::vector<GeometryRecord> geoRecords;

		// A cutout (glTF MASK) instance uses an OMM-carrying BLAS on an OMM-capable device (the micromap resolves
		// coverage during traversal, any-hit only on UNKNOWN edges); elsewhere it falls back to the
		// FORCE_NO_OPAQUE any-hit path. kOmmSubdivisionLevel = 4^level microtriangles per triangle.
		const bool ommDevice = Renderer::IsOpacityMicromapSupported() && ommEnabled;
		constexpr uint32_t kOmmSubdivisionLevel = 3;
		uint32_t ommDeferred = 0; // cutout instances on the any-hit fallback this frame because their albedo isn't resident
		for (auto view = reg.view<TransformComponent, MeshComponent>(); const entt::entity e : view)
		{
			const auto& mc = reg.Read<MeshComponent>(e);
			if (!mc.MeshInstance) // mesh not resolved yet (async load in flight)
			{
				continue;
			}

			// Read the material up front: it decides both the geometry record and whether this is a cutout
			// instance (which picks the OMM BLAS and drops FORCE_NO_OPAQUE). May be null (async) — then the
			// record stays a BaseColor-white fallback and the instance is treated as opaque.
			const Material::Constants* c = nullptr;
			if (const auto* matc = reg.try_get_const<MaterialComponent>(e); matc && matc->MaterialInstance)
			{
				c = &matc->MaterialInstance->GetConstants();
			}
			const bool masked = c && c->AlphaMaskEnabled != 0;
			// The OMM bakes the albedo alpha ONCE at BLAS build and caches the result. If the albedo texture isn't
			// resident yet (its slot still holds the async magenta placeholder, alpha = 1), that bake classifies
			// every microtriangle OPAQUE -> solid cutouts, cached forever. Defer to the any-hit fallback until the
			// real pixels land; the streaming rebuild above re-enters here and bakes the OMM once resident.
			const bool albedoReady = !masked || assets.IsTextureSlotResident(c->AlbedoTextureIndex);
			const bool useOmm = masked && ommDevice && albedoReady;
			if (masked && ommDevice && !albedoReady)
			{
				++ommDeferred;
			}

			// OMM path builds a GPU-baked micromap BLAS; it returns null while the bake compute pipeline is still
			// compiling, so fall back to the plain BLAS + FORCE_NO_OPAQUE any-hit that frame (retried next build).
			Ref<BLAS> blas;
			bool ommBuilt = false;
			if (useOmm)
			{
				blas = mc.MeshInstance->GetOrBuildOmmBlas(kOmmSubdivisionLevel, c->AlbedoTextureIndex, c->AlphaCutoff,
				                                          c->BaseColor.a);
				ommBuilt = blas != nullptr;
			}
			if (!ommBuilt)
			{
				blas = mc.MeshInstance->GetOrBuildBLAS();
			}
			if (!blas)
			{
				continue;
			}

			const auto& tc = reg.Read<TransformComponent>(e);
			const glm::mat4 model = tc.GetTransformMatrix();
			instances.push_back({model, blas->GetDeviceAddress()});
			instanceEntities.push_back(e);
			// Masked geometry must traverse non-opaque so the alpha test runs. With an OMM the micromap drives
			// opacity (and FORCE_NO_OPAQUE would OVERRIDE it, forcing any-hit everywhere), so only the non-OMM
			// fallback sets the instance flag; the OMM BLAS is already built non-opaque.
			instances.back().ForceNonOpaque = masked && !ommBuilt;

			GeometryRecord rec{};
			rec.VertexAddress = mc.MeshInstance->GetVertexBuffer()->GetGPUAddress();
			rec.IndexAddress = mc.MeshInstance->GetIndexBuffer()->GetGPUAddress();
			rec.Model = model;
			if (c)
			{
				rec.AlbedoTextureIndex = c->AlbedoTextureIndex;
				rec.BaseColor = c->BaseColor;
				rec.AlphaMaskEnabled = c->AlphaMaskEnabled;
				rec.AlphaCutoff = c->AlphaCutoff;
				// PBR block (#153) for the reference path tracer: the full material so PT hits shade with the
				// real BRDF (metallic/roughness/emissive + normal/MR maps), not just albedo.
				rec.MetallicRoughnessTextureIndex = c->MetallicRoughnessTextureIndex;
				rec.NormalTextureIndex = c->NormalTextureIndex;
				rec.EmissiveTextureIndex = c->EmissiveTextureIndex;
				rec.Metallic = c->Metallic;
				rec.Roughness = c->Roughness;
				rec.EmissiveR = c->EmissiveColor.r;
				rec.EmissiveG = c->EmissiveColor.g;
				rec.EmissiveB = c->EmissiveColor.b;
			}
			geoRecords.push_back(rec);
		}

		// Publish the index->entity table for RT picking (consumed by the editor). Rebuilt every TLAS build
		// so it never drifts from what the GPU traces.
		SingletonView<TlasInstanceMapSingleton>().Instances = std::move(instanceEntities);

		// Publish the geometry table (consumed by RendererService -> DefaultLit / GI / Reflection). Grow the
		// GPU buffer when the instance count outgrows it (device-address Storage so the shader can RawBufferLoad
		// records); address 0 when RT is off or the scene is empty so the shader falls back to the sky cube.
		auto& reflGeo = SingletonView<ReflectionGeometrySingleton>();
		if (!geoRecords.empty())
		{
			const uint32_t needed = static_cast<uint32_t>(geoRecords.size());
			if (!reflGeo.Table || reflGeo.Capacity < needed)
			{
				reflGeo.Capacity = needed;
				// DEVICE-LOCAL (hostVisible=false): the table is read on the GPU hot path — every reflection/GI
				// ray that hits geometry does several RawBufferLoads here plus vertex/index fetches — but written
				// only on a rare rebuild (scene edit). A host-visible allocation lands in system RAM on a discrete
				// GPU, so those hot reads would cross PCIe; device-local keeps them in VRAM. SetData takes the
				// staging-upload path for a device-local buffer (one copy on the rare write), the right tradeoff
				// for hot-read/rare-write data (mirrors how meshes' own vertex/index buffers are stored).
				reflGeo.Table = Buffer::Create(static_cast<size_t>(needed) * sizeof(GeometryRecord),
				                               BufferUsage::Storage, nullptr, false, "ReflectionGeometryTable");
			}
			// Drain before overwriting the table. The reflection/GI shaders read it by device address, and up to
			// framesInFlight prior frames may still be reading LAST rebuild's contents on the GPU. Rewriting it
			// without a drain tore a record mid-read on a rebuild (e.g. deleting a mesh while RT reflections are
			// on) → the shader RawBufferLoad'd a half-written VertexAddress → GPU READ_INVALID →
			// VK_ERROR_DEVICE_LOST. Confirmed via VK_EXT_device_fault (READ_INVALID at a garbage address). Sync
			// validation can't see it (it's a raw-device-address read, not a tracked VkBuffer hazard). The drain
			// is needed for EITHER allocation type: even the device-local staging copy writes m_Buffer on the
			// graphics queue and can overlap a prior frame's read without a cross-submit barrier. Rebuilds are
			// rare, so a full wait is cheap and matches how VulkanTlas::Build / the AS destructors serialize.
			Renderer::WaitIdle();
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

		// Cutouts whose albedo hasn't streamed in bake no OMM this frame and run on the any-hit fallback (correct,
		// just unoptimized); they re-bake once resident. Log the count only when it changes (settles to 0 as
		// textures land) so a slow load doesn't spam.
		if (ommDeferred != m_LastOmmDeferredLogged)
		{
			if (ommDeferred > 0)
			{
				SS_CORE_INFO("OMM bake deferred for {} cutout instance(s) awaiting albedo residency.", ommDeferred);
			}
			m_LastOmmDeferredLogged = ommDeferred;
		}
	}
}
