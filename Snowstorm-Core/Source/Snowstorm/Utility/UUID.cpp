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
		// Stored as a decimal string in JSON. Malformed input must NOT throw or abort the load: this is
		// called per scene-entity ID and per asset handle, so a single bad value would otherwise terminate
		// the whole scene/asset deserialization (std::stoull throws; the old trailing-garbage assert was
		// debug-only and silently accepted "123abc" as 123 in release). Fail soft to UUID(0) — callers
		// already treat the zero handle as "skip/unresolved" — and log once so it's visible.
		size_t idx = 0;
		UnderlyingType v = 0;

		try
		{
			v = std::stoull(s, &idx, 10);
		}
		catch (const std::exception&)
		{
			SS_CORE_ERROR("Invalid UUID string '{}'", s);
			return UUID(0);
		}

		if (idx != s.size())
		{
			SS_CORE_ERROR("Invalid UUID string '{}' (trailing characters)", s);
			return UUID(0);
		}

		return UUID(v);
	}
}
