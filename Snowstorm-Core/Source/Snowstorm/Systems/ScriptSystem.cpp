#include "ScriptSystem.hpp"

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/World/ScriptableEntity.hpp"

namespace Snowstorm
{
	void ScriptSystem::Execute(const Timestep ts)
	{
		const auto nativeScriptView = View<NativeScriptComponent>();
		const auto nativeScriptInitView = InitView<NativeScriptComponent>();
		const auto nativeScriptFiniView = FiniView<NativeScriptComponent>();

		// Instantiate scripts if not already created
		for (const auto entity : nativeScriptInitView)
		{
			auto [scriptComponent] = nativeScriptView.get(entity);
			SS_CORE_ASSERT(scriptComponent.Instance == nullptr);

			scriptComponent.InstantiateScript();
			m_Live[entity] = scriptComponent.Instance;
			scriptComponent.Instance->m_Entity = Entity{entity, m_World};
			scriptComponent.Instance->OnCreate();
		}

		// Update all scripts
		for (const auto entity : nativeScriptView)
		{
			auto [scriptComponent] = nativeScriptView.get(entity);
			SS_CORE_ASSERT(scriptComponent.Instance);

			scriptComponent.Instance->OnUpdate(ts);
		}

		// The component is already gone, so the instance comes from m_Live. Its DestroyScript callback is not
		// usable: it captured the removed component's address.
		for (const auto entity : nativeScriptFiniView)
		{
			const auto live = m_Live.find(entity);
			if (live == m_Live.end())
			{
				continue;
			}

			live->second->OnDestroy();
			delete live->second;
			m_Live.erase(live);
		}
	}

	void ScriptSystem::DeclareAccess(SystemAccess& a) const
	{
		a.Structural();
	}
}
