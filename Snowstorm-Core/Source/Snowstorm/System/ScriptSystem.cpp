#include "ScriptSystem.hpp"

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/World/ScriptableEntity.h"

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
			scriptComponent.Instance->OnCreate();
		}

		// Update all scripts
		for (const auto entity : nativeScriptView)
		{
			auto [scriptComponent] = nativeScriptView.get(entity);
			SS_CORE_ASSERT(scriptComponent.Instance);

			scriptComponent.Instance->OnUpdate(ts);
		}

		// Destroy scripts that are removed
		for (const auto entity : nativeScriptFiniView)
		{
			auto [scriptComponent] = nativeScriptView.get(entity);
			SS_CORE_ASSERT(scriptComponent.Instance);

			scriptComponent.Instance->OnDestroy();
			scriptComponent.DestroyScript();
		}
	}
}
