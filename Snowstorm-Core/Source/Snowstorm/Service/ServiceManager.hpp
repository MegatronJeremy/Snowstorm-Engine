#pragma once

#include <ranges>
#include <vector>

#include "Service.hpp"

#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	class ServiceManager
	{
	public:
		template <typename T, typename... Args>
		void RegisterService(Args&&... args)
		{
			static_assert(std::is_base_of_v<Service, T>, "T must inherit from Service");

			if (const std::type_index typeIndex(typeid(T)); !m_Services.contains(typeIndex))
			{
				m_Services[typeIndex].reset(new T(std::forward<Args>(args)...));
			}
		}

		template <typename T>
		[[nodiscard]] T& GetService()
		{
			const std::type_index typeIndex(typeid(T));

			SS_ASSERT(m_Services.contains(typeIndex), "Service not registered!");

			return *static_cast<T*>(m_Services[typeIndex].get());
		}

		template <typename T>
		[[nodiscard]] bool ServiceRegistered() const
		{
			const std::type_index typeIndex(typeid(T));
			return m_Services.contains(typeIndex);
		}

		void ExecuteUpdate(const Timestep ts) const
		{
			for (const auto& service : m_Services | std::views::values)
			{
				service->OnUpdate(ts);
			}
		}

		void ExecutePostUpdate(const Timestep ts) const
		{
			for (const auto& service : m_Services | std::views::values)
			{
				service->PostUpdate(ts);
			}
		}

	private:
		std::unordered_map<std::type_index, Scope<Service>> m_Services;
	};
}
