#pragma once

#include <cstdint>
#include <string>

#include <functional>

namespace Snowstorm
{
	// Simple 64-bit UUID for now. Can be upgraded later (e.g. 128-bit) without changing usage patterns.
	class UUID
	{
	public:
		using UnderlyingType = uint64_t;

		UUID();
		explicit UUID(UnderlyingType value);

		bool operator==(const UUID&) const noexcept = default;
		operator UnderlyingType() const noexcept { return m_Value; }

		UnderlyingType Value() const noexcept { return m_Value; }

		std::string ToString() const; 
		static UUID FromString(const std::string& s);

	private:
		UnderlyingType m_Value = 0;
	};
}

// Enables UUID to be used as a hash key
template<>
struct std::hash<Snowstorm::UUID>
{
	size_t operator()(const Snowstorm::UUID& uuid) const noexcept
	{
		return std::hash<Snowstorm::UUID::UnderlyingType>{}(uuid.Value());
	}
};
