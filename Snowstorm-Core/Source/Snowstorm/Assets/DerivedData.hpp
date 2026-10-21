#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <ostream>
#include <span>
#include <string_view>

namespace Snowstorm
{
	// Content address of a cooked artifact: a hash of the source bytes, the artifact's format version and
	// every other input that changes the output (Unreal's DDC key model). Different versions of one asset
	// get different entries, so two checkouts that disagree about an asset never overwrite each other.
	[[nodiscard]] uint64_t DerivedDataKey(uint64_t sourceHash, uint32_t formatVersion, std::span<const std::byte> inputs);

	// <cache root>/<kind>/<16 hex digits of key><extension>
	[[nodiscard]] std::filesystem::path DerivedDataPath(std::string_view kind, uint64_t key, std::string_view extension);

	// Writes through a temp file unique to this call, then renames it over `path`, so a reader never sees
	// a partial file and two writers of one entry never share a temp. Writers of one content-addressed
	// entry produce identical bytes, so a rename that loses to an existing entry still counts as written.
	bool WriteFileAtomic(const std::filesystem::path& path, const std::function<bool(std::ostream&)>& write);
}
