#pragma once
#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"

namespace Snowstorm
{
	// Application-scoped subsystem: one instance per type for the lifetime of the Application, held by
	// ServiceManager. Ticking is OPT-IN — OnUpdate/PostUpdate default to no-ops so a service that is pure
	// shared state (GPU resource caches, the renderer) can register without pretending to have a per-frame
	// lifecycle. Frame-driven services (e.g. ImGuiService) override them. Cf. Unreal's USubsystem, where a
	// subsystem is app/engine-scoped and per-frame Tick is optional. (SingletonManager is the World-scoped
	// analogue for per-scene state.) The virtual destructor comes from NonCopyable (TypeMap deletes entries
	// polymorphically).
	class Service : public NonCopyable
	{
	public:
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void PostUpdate(Timestep ts) { (void)ts; }
	};
}
