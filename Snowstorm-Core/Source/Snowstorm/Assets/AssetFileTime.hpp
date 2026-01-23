#pragma once

#include <filesystem>
#include <cstdint>

namespace Snowstorm
{
	inline uint64_t GetFileWriteTimeU64(const std::filesystem::path& p)
	{
		std::error_code ec;
		const auto ft = std::filesystem::last_write_time(p, ec);
		if (ec) return 0;

		// Convert file_time_type to a count. This is implementation-defined but stable enough per machine.
		// Good for cache invalidation. If you later want cross-machine stability, switch to hashing file contents.
		return static_cast<uint64_t>(ft.time_since_epoch().count());
	}
}
