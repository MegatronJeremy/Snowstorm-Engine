#include "UUID.hpp"

#include <random>
#include <stdexcept>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		UUID::UnderlyingType GenerateUUID()
		{
			// Reasonable default generator; good enough for editor scene identity.
			// If you later want deterministic UUIDs (for prefabs), swap this out.
			thread_local std::mt19937_64 rng{std::random_device{}()};
			thread_local std::uniform_int_distribution<UUID::UnderlyingType> dist;
			return dist(rng);
		}
	}

	UUID::UUID()
		: m_Value(GenerateUUID())
	{
	}

	UUID::UUID(const UnderlyingType value)
		: m_Value(value)
	{
	}

	std::string UUID::ToString() const
	{
		return std::to_string(m_Value);
	}

	UUID UUID::FromString(const std::string& s)
	{
		// stored as decimal string in JSON
		size_t idx = 0;
		UnderlyingType v = 0;

		v = std::stoull(s, &idx, 10);

		if (idx != s.size())
		{
			SS_CORE_ASSERT(false, "Invalid UUID string");
		}

		return UUID(v);
	}
}
