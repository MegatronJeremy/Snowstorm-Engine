#include "TlasBuildSystem.hpp"

#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
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
		// Only maintain the TLAS while RT shadows are actually active — nothing samples it in Shadow Map / Off
		// mode, so building it there is pure waste. Folds in the device-support check (ShadowsRTActive() is
		// false on a non-RT GPU). Track the state so the OFF->ON transition can force a rebuild below.
		const bool rtActive = CVars::ShadowsRTActive();
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
		std::vector<TLASInstance> instances;
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
			instances.push_back({tc.GetTransformMatrix(), blas->GetDeviceAddress()});
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
