#pragma once

#include "Snowstorm/Utility/Hash.hpp"

#include <filesystem>
#include <cstdint>

namespace Snowstorm
{
	inline uint64_t GetFileWriteTimeU64(const std::filesystem::path& p)
	{
		std::error_code ec;
		const auto ft = std::filesystem::last_write_time(p, ec);
		if (ec)
			return 0;

		// Convert file_time_type to a count. This is implementation-defined but stable enough per machine.
		// Good as a CHEAP GATE only; SourceIsUnchanged below is what actually decides freshness.
		return static_cast<uint64_t>(ft.time_since_epoch().count());
	}

	// Whether a cooked artifact built from `path` is still valid, given what was recorded when it was
	// written. Two levels, and both are load-bearing:
	//
	//   mtime matches  -> valid, and the source is never opened. This is the common case and it is what
	//                     keeps the cache worth having: hashing on every load would read the very file
	//                     the cache exists to avoid reading.
	//   mtime differs  -> hash the bytes. Equal content means the artifact is still good and only the
	//                     timestamp moved, which is exactly what a fresh checkout, a copy or a touch
	//                     does. This is what makes a cooked artifact SHIPPABLE rather than valid only on
	//                     the machine that produced it.
	//
	// A recorded hash of 0 means the artifact predates hashing, so it can only be trusted on the mtime.
	inline bool SourceIsUnchanged(const std::filesystem::path& path, const uint64_t recordedWriteTime,
	                              const uint64_t recordedHash)
	{
		if (recordedWriteTime != 0 && recordedWriteTime == GetFileWriteTimeU64(path))
			return true;
		if (recordedHash == 0)
			return false;
		return recordedHash == HashFileContents(path);
	}
}
