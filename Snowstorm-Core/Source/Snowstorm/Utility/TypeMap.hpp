#pragma once

#include <typeindex>
#include <unordered_map>
#include <vector>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp" // SS_CORE_ASSERT expands to SS_CORE_ERROR

namespace Snowstorm
{
	// Owning container keyed by C++ type: at most one instance of each concrete type T
	// (deriving Base) is stored. Lookup is O(1) by type; iteration preserves insertion order
	// so consumers get a deterministic update order. Shared by ServiceManager and
	// SingletonManager (Base must have a virtual destructor — it is deleted polymorphically).
	template <typename Base>
	class TypeMap
	{
	public:
		// Constructs T in place if absent and returns it; returns the existing instance otherwise.
		template <typename T, typename... Args>
		T& Emplace(Args&&... args)
		{
			static_assert(std::is_base_of_v<Base, T>, "T must derive from Base");

			const std::type_index typeIndex(typeid(T));
			if (const auto it = m_Lookup.find(typeIndex); it != m_Lookup.end())
			{
				return *static_cast<T*>(it->second);
			}

			Scope<T> owned = CreateScope<T>(std::forward<Args>(args)...);
			T* raw = owned.get();
			m_Lookup.emplace(typeIndex, raw);
			m_Items.push_back(std::move(owned));
			return *raw;
		}

		template <typename T>
		[[nodiscard]] T& Get() const
		{
			const auto it = m_Lookup.find(std::type_index(typeid(T)));
			SS_CORE_ASSERT(it != m_Lookup.end(), "TypeMap: type not registered");
			return *static_cast<T*>(it->second);
		}

		template <typename T>
		[[nodiscard]] bool Contains() const
		{
			return m_Lookup.contains(std::type_index(typeid(T)));
		}

		// Iterate stored instances in insertion order.
		template <typename Fn>
		void ForEach(Fn&& fn) const
		{
			for (const auto& item : m_Items)
			{
				fn(*item);
			}
		}

	private:
		std::vector<Scope<Base>> m_Items;                    // ownership, insertion order
		std::unordered_map<std::type_index, Base*> m_Lookup; // non-owning fast lookup
	};
}
