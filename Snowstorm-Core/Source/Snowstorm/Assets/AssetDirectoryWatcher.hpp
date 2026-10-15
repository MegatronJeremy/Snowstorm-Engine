#pragma once

#include "Snowstorm/Assets/AssetFileTime.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Snowstorm
{
	struct FileStamp
	{
		uint64_t WriteTime = 0;
		uint64_t Size = 0;

		bool operator==(const FileStamp& o) const { return WriteTime == o.WriteTime && Size == o.Size; }
		bool operator!=(const FileStamp& o) const { return !(*this == o); }
	};

	using DirectorySnapshot = std::map<std::string, FileStamp>;

	// Stats every regular file under `root` that `accept` takes. Directories named in `skipDirName` are not
	// entered. Polling is what makes this portable and immune to ReadDirectoryChangesW overflow on a bulk
	// copy; the snapshot is a plain value so it can be taken on a worker thread.
	inline DirectorySnapshot ScanDirectory(const std::filesystem::path& root, const std::string& skipDirName,
	                                       const std::function<bool(const std::filesystem::path&)>& accept)
	{
		DirectorySnapshot out;
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
		     it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec)
				continue;
			if (it->is_directory(ec))
			{
				if (it->path().filename() == skipDirName)
					it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(ec) || !accept(it->path()))
				continue;
			FileStamp stamp;
			stamp.WriteTime = GetFileWriteTimeU64(it->path());
			stamp.Size = static_cast<uint64_t>(it->file_size(ec));
			out[it->path().generic_string()] = stamp;
		}
		return out;
	}

	// Turns a stream of snapshots into settled changes. Exporters and editors write a file in several bursts,
	// so a new or modified file is reported only once its stamp has stayed unchanged for `settleSeconds`;
	// importing mid-write would register a truncated file.
	class ChangeDebouncer
	{
	public:
		struct Changes
		{
			std::vector<std::string> Settled; // new or modified, stable
			std::vector<std::string> Removed;

			bool Empty() const { return Settled.empty() && Removed.empty(); }
		};

		explicit ChangeDebouncer(const double settleSeconds = 0.5)
		    : m_Settle(settleSeconds)
		{
		}

		// Treats `snapshot` as already handled (files that exist when the watch starts).
		void Seed(DirectorySnapshot snapshot)
		{
			m_Known = std::move(snapshot);
			m_Pending.clear();
		}

		Changes Update(const DirectorySnapshot& current, const double now)
		{
			Changes changes;

			for (const auto& [path, stamp] : current)
			{
				const auto known = m_Known.find(path);
				if (known != m_Known.end() && known->second == stamp)
				{
					m_Pending.erase(path);
					continue;
				}

				auto pending = m_Pending.find(path);
				if (pending == m_Pending.end() || pending->second.Stamp != stamp)
				{
					m_Pending[path] = {stamp, now};
					continue;
				}

				if (now - pending->second.Since >= m_Settle)
				{
					m_Known[path] = stamp;
					m_Pending.erase(pending);
					changes.Settled.push_back(path);
				}
			}

			for (auto it = m_Known.begin(); it != m_Known.end();)
			{
				if (current.contains(it->first))
				{
					++it;
					continue;
				}
				changes.Removed.push_back(it->first);
				m_Pending.erase(it->first);
				it = m_Known.erase(it);
			}
			for (auto it = m_Pending.begin(); it != m_Pending.end();)
				it = current.contains(it->first) ? std::next(it) : m_Pending.erase(it);

			return changes;
		}

	private:
		struct Pending
		{
			FileStamp Stamp;
			double Since = 0.0;
		};

		double m_Settle;
		DirectorySnapshot m_Known;
		std::map<std::string, Pending> m_Pending;
	};
}
