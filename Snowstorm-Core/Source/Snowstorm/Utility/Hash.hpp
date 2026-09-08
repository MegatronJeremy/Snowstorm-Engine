#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace Snowstorm
{
	// FNV-1a. Not cryptographic and not meant to be: this identifies content for cache lookups, where the
	// cost of a collision is a stale artifact, not a security failure.
	[[nodiscard]] uint64_t Hash64(const void* data, size_t size);

	// Hash of a file's bytes, or 0 if it cannot be read. 0 therefore means "unknown", and a caller must
	// treat it as a cache miss rather than as a value that can match.
	[[nodiscard]] uint64_t HashFileContents(const std::filesystem::path& path);
}
