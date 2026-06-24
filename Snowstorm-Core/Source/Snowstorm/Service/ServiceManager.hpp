#pragma once

#include "Service.hpp"

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Utility/TypeMap.hpp"

namespace Snowstorm
{
	// Application-scoped services: one instance per type, living for the lifetime of the
	// Application. OnUpdate/PostUpdate run every frame in registration order. (Cf. SingletonManager,
	// which holds world-scoped, lifecycle-free state — both share TypeMap.)
	class ServiceManager
	{
	public:
		template <typename T, typename... Args>
		void RegisterService(Args&&... args)
		{
			m_Services.Emplace<T>(std::forward<Args>(args)...);
		}

		template <typename T>
		[[nodiscard]] T& GetService()
		{
			return m_Services.Get<T>();
		}

		template <typename T>
		[[nodiscard]] bool ServiceRegistered() const
		{
			return m_Services.Contains<T>();
		}

		void ExecuteUpdate(const Timestep ts) const
		{
			m_Services.ForEach([ts](Service& service)
			                   { service.OnUpdate(ts); });
		}

		void ExecutePostUpdate(const Timestep ts) const
		{
			m_Services.ForEach([ts](Service& service)
			                   { service.PostUpdate(ts); });
		}

	private:
		TypeMap<Service> m_Services;
	};
}
