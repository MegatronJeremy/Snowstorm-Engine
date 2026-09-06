#pragma once

#include "Snowstorm/Core/Timestep.hpp"

namespace Snowstorm
{
	class World;

	// A game's entry into the runtime host. RuntimeLayer owns one module and calls it at the three points a
	// game needs: once the World, active project, startup scene and viewport camera all exist (OnAttach),
	// every frame before the World's systems run (OnUpdate), and at shutdown while the World is still alive
	// (OnDetach). Spawn entities, register systems (world.GetSystemManager().RegisterSystem<T>(phase)) and
	// subscribe to events from OnAttach; the host stays generic and the game never subclasses the layer.
	class IGameModule
	{
	public:
		virtual ~IGameModule() = default;

		[[nodiscard]] virtual const char* Name() const = 0;

		virtual void OnAttach(World&)
		{
		}

		virtual void OnDetach(World&)
		{
		}

		virtual void OnUpdate(World&, Timestep)
		{
		}
	};
}
