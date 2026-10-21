#include "SourceHashIndex.hpp"

#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Assets/DerivedData.hpp"
#include "Snowstorm/Utility/EnginePaths.hpp"
#include "Snowstorm/Utility/Hash.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kIndexVersion = 1;
	}

	SourceHashIndex::SourceHashIndex(std::filesystem::path indexFile) : m_IndexFile(std::move(indexFile))
	{
	}

	SourceHashIndex& SourceHashIndex::Get()
	{
		static SourceHashIndex s_Index(EngineAssetPath("Engine/cache/source-index.json"));
		return s_Index;
	}

	uint64_t SourceHashIndex::Hash(const std::filesystem::path& source)
	{
		std::error_code ec;
		const uint64_t size = std::filesystem::file_size(source, ec);
		if (ec)
		{
			return 0;
		}
		const uint64_t writeTime = GetFileWriteTimeU64(source);
		const std::string key = std::filesystem::absolute(source, ec).lexically_normal().generic_string();

		{
			std::lock_guard lock(m_Mutex);
			LoadLocked();
			if (const auto it = m_Entries.find(key);
			    it != m_Entries.end() && it->second.Size == size && it->second.WriteTime == writeTime)
			{
				return it->second.Hash;
			}
		}

		const uint64_t hash = HashFileContents(source);
		if (hash != 0)
		{
			std::lock_guard lock(m_Mutex);
			m_Entries[key] = Entry{size, writeTime, hash};
			m_Dirty = true;
		}
		return hash;
	}

	void SourceHashIndex::Save()
	{
		std::lock_guard lock(m_Mutex);
		if (!m_Dirty)
		{
			return;
		}

		nlohmann::json entries = nlohmann::json::object();
		for (const auto& [path, e] : m_Entries)
		{
			entries[path] = {{"Size", e.Size}, {"WriteTime", e.WriteTime}, {"Hash", e.Hash}};
		}
		const nlohmann::json root = {{"Version", kIndexVersion}, {"Entries", std::move(entries)}};

		if (WriteFileAtomic(m_IndexFile, [&](std::ostream& out)
		                    { return static_cast<bool>(out << root.dump()); }))
		{
			m_Dirty = false;
		}
	}

	void SourceHashIndex::LoadLocked()
	{
		if (m_Loaded)
		{
			return;
		}
		m_Loaded = true;

		std::ifstream in(m_IndexFile);
		if (!in.is_open())
		{
			return;
		}
		const nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
		if (!root.is_object() || root.value("Version", 0u) != kIndexVersion || !root.contains("Entries") ||
		    !root["Entries"].is_object())
		{
			return;
		}
		for (const auto& [path, e] : root["Entries"].items())
		{
			const auto isU64 = [&e](const char* field)
			{ return e.contains(field) && e[field].is_number_unsigned(); };
			if (e.is_object() && isU64("Size") && isU64("WriteTime") && isU64("Hash"))
			{
				m_Entries[path] = Entry{e["Size"].get<uint64_t>(), e["WriteTime"].get<uint64_t>(), e["Hash"].get<uint64_t>()};
			}
		}
	}
}
