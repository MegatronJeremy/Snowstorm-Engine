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
			scriptComponent.Instance->m_Entity = Entity{entity, m_World};
			m_Live[entity] = scriptComponent.Instance;
			scriptComponent.Instance->OnCreate();
		}

		// Update all scripts
		for (const auto entity : nativeScriptView)
		{
			auto [scriptComponent] = nativeScriptView.get(entity);
			SS_CORE_ASSERT(scriptComponent.Instance);

			scriptComponent.Instance->OnUpdate(ts);
		}

		// FiniView lists entities whose component is already gone, so the instance comes from m_Live.
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
