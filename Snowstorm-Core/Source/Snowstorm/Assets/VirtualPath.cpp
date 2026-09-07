#include "VirtualPath.hpp"

#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Utility/EnginePaths.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace Snowstorm::VirtualPath
{
	namespace
	{
		namespace fs = std::filesystem;

		// The whole table. Roots are resolved per call because /Game/ follows whichever project is
		// active and /Engine/ follows the engine root, neither of which is known at static-init time.
		constexpr std::array kPrefixes{"/engine/", "/game/", "/cache/"};

		std::string Lower(const std::string_view s)
		{
			std::string out(s);
			std::ranges::transform(out, out.begin(), [](const unsigned char c)
			                       { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// The prefix a path starts with, lower-cased, or empty. Longest match wins; with these three
		// there is no overlap, but the rule is what keeps the table order-free if one is ever nested
		// under another.
		std::string_view MatchPrefix(const std::string_view lowered)
		{
			std::string_view best;
			for (const auto* p : kPrefixes)
			{
				const std::string_view prefix(p);
				if (lowered.starts_with(prefix) && prefix.size() > best.size())
				{
					best = prefix;
				}
			}
			return best;
		}
	}

	fs::path MountRoot(const std::string_view prefix)
	{
		const std::string key = Lower(prefix);
		if (key == "/engine/")
		{
			return GetEngineRoot() / "Engine";
		}
		if (key == "/cache/")
		{
			return GetEngineRoot() / "Engine" / "cache";
		}
		if (key == "/game/")
		{
			const Ref<Project> project = Project::GetActive();
			return project ? project->GetAssetDirectory() : fs::path{};
		}
		return {};
	}

	bool IsVirtual(const std::string_view path)
	{
		return !MatchPrefix(Lower(path)).empty();
	}

	fs::path Resolve(const std::string_view virtualPath)
	{
		const std::string lowered = Lower(virtualPath);
		const std::string_view prefix = MatchPrefix(lowered);
		if (prefix.empty())
		{
			return {};
		}

		const fs::path root = MountRoot(prefix);
		if (root.empty())
		{
			return {}; // /Game/ before a project is active, which is a caller error rather than a miss
		}

		// Take the tail from the ORIGINAL string: only the prefix match is case-insensitive, because on a
		// case-sensitive filesystem the rest is not ours to fold.
		return root / fs::path(std::string(virtualPath.substr(prefix.size()))).lexically_normal();
	}

	std::optional<std::string> Virtualize(const fs::path& absolute)
	{
		const fs::path normalized = absolute.lexically_normal();
		const std::string lowered = Lower(normalized.generic_string());

		std::optional<std::string> best;
		size_t bestRootLength = 0;

		for (const auto* p : kPrefixes)
		{
			const fs::path root = MountRoot(p);
			if (root.empty())
			{
				continue;
			}
			const std::string rootLower = Lower(root.lexically_normal().generic_string());
			if (!lowered.starts_with(rootLower) || rootLower.size() < bestRootLength)
			{
				continue;
			}

			std::string tail = normalized.generic_string().substr(rootLower.size());
			if (!tail.empty() && tail.front() == '/')
			{
				tail.erase(0, 1);
			}
			best = std::string(p) + tail;
			bestRootLength = rootLower.size();
		}

		// Restore the canonical prefix casing, since kPrefixes is stored lower-cased for matching.
		if (best)
		{
			if (best->starts_with("/engine/"))
			{
				best->replace(0, 8, "/Engine/");
			}
			else if (best->starts_with("/game/"))
			{
				best->replace(0, 6, "/Game/");
			}
			else if (best->starts_with("/cache/"))
			{
				best->replace(0, 7, "/Cache/");
			}
		}
		return best;
	}

	AssetRef SplitSubResource(const std::string_view path)
	{
		constexpr std::string_view marker = "?submesh=";
		const size_t pos = path.find(marker);
		if (pos == std::string_view::npos)
		{
			return {std::string(path), -1};
		}
		AssetRef ref;
		ref.Path = std::string(path.substr(0, pos));
		ref.SubResource = std::stoi(std::string(path.substr(pos + marker.size())));
		return ref;
	}

	std::string JoinSubResource(const std::string_view path, const int subResource)
	{
		return std::string(path) + "?submesh=" + std::to_string(subResource);
	}

	std::string NormalizeKey(const std::string_view path)
	{
		return Lower(fs::path(std::string(path)).lexically_normal().generic_string());
	}
}
