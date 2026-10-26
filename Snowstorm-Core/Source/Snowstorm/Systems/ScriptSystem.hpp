#pragma once

#include <Snowstorm/ECS/System.hpp>

#include <unordered_map>

namespace Snowstorm
{
	class ScriptableEntity;

	// Runs entity scripts. Simulation: only ticks in Play mode (RunsInEditMode == false).
	class ScriptSystem final : public System
	{
	public:
		explicit ScriptSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		void DeclareAccess(SystemAccess& a) const override;

		[[nodiscard]] bool RunsInEditMode() const override { return false; }

	private:
		// Instances created by this system, so OnDestroy still reaches one after Remove<NativeScriptComponent>().
		std::unordered_map<entt::entity, ScriptableEntity*> m_Live;
	};
}
