#include "DroppedFiles.hpp"

#include <mutex>
#include <utility>

namespace Snowstorm::DroppedFiles
{
	namespace
	{
		std::mutex s_Mutex;
		std::vector<std::filesystem::path> s_Paths;
	}

	void Push(const std::filesystem::path& path)
	{
		std::scoped_lock lock(s_Mutex);
		s_Paths.push_back(path);
	}

	std::vector<std::filesystem::path> TakeAll()
	{
		std::scoped_lock lock(s_Mutex);
		return std::exchange(s_Paths, {});
	}
}
