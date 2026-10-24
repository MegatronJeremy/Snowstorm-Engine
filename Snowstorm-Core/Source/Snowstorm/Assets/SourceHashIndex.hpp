#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Snowstorm
{
	// Content hash of a source file that reads the file only when its size or mtime changed, the way git's
	// index avoids rehashing an unchanged tree. Cooked artifacts are named by content hash, so without this a
	// cache hit would read the very source the cache exists to avoid reading. Per checkout: mtimes differ
	// between clones of the same bytes, which is why the stamps live here and not beside the shared artifact.
	class SourceHashIndex
	{
	public:
		explicit SourceHashIndex(std::filesystem::path indexFile);

		// The process-wide index, persisted to Engine/cache/source-index.json.
		static SourceHashIndex& Get();

		// 0 when the file cannot be read, which callers treat as a cache miss.
		[[nodiscard]] uint64_t Hash(const std::filesystem::path& source);

		// Persists entries added since the last save. Hashing is all an unsaved entry costs.
		void Save();

	private:
		struct Entry
		{
			uint64_t Size = 0;
			uint64_t WriteTime = 0;
			uint64_t Hash = 0;
		};

		void LoadLocked();

		std::filesystem::path m_IndexFile;
		std::mutex m_Mutex;
		std::unordered_map<std::string, Entry> m_Entries;
		bool m_Loaded = false;
		bool m_Dirty = false;
	};
}
