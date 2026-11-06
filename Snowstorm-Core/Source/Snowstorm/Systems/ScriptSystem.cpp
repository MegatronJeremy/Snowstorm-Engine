#include "ScriptSystem.hpp"

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/World/ScriptableEntity.hpp"

namespace Snowstorm
{
	ScriptSystem::~ScriptSystem()
	{
		for (const auto& [entity, instance] : m_Live)
		{
			delete instance;
		}
	}

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

		// registry.destroy, a registry clear and a scene reload leave no removal record, so the instance would
		// otherwise live until teardown. Its entity is gone, so OnDestroy (which may reach for its components)
		// is not called, as at teardown.
		auto& reg = m_World->GetRegistry();
		std::erase_if(m_Live,
		              [&](const auto& entry)
		              {
			              const auto [entity, instance] = entry;
			              const auto* script = reg.valid(entity) ? reg.try_get<NativeScriptComponent>(entity) : nullptr;
			              if (script && script->Instance == instance)
			              {
				              return false;
			              }
			              delete instance;
			              return true;
		              });
	}

	void ScriptSystem::DeclareAccess(SystemAccess& a) const
	{
		a.Structural();
	}
}
