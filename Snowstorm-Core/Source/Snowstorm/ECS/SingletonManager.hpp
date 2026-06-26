#pragma once

#include "Singleton.hpp"

#include "Snowstorm/Utility/TypeMap.hpp"

namespace Snowstorm
{
	// World-scoped singletons: one instance per type, living for the lifetime of a World.
	// Plain cross-cutting state with no per-frame lifecycle (cf. ServiceManager, which is
	// application-scoped and ticks its entries). Both share TypeMap.
	class SingletonManager
	{
	public:
		template <typename T, typename... Args>
		void RegisterSingleton(Args&&... args)
		{
			m_Singletons.Emplace<T>(std::forward<Args>(args)...);
		}

		template <typename T>
		[[nodiscard]] T& GetSingleton()
		{
			return m_Singletons.Get<T>();
		}

		template <typename T>
		[[nodiscard]] bool HasSingleton() const
		{
			return m_Singletons.Contains<T>();
		}

	private:
		TypeMap<Singleton> m_Singletons;
	};
}
