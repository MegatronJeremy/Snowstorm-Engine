#include "Snowstorm/Assets/AssetImport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

namespace Snowstorm
{
	namespace
	{
		struct ExtensionRow
		{
			const char* Ext;
			AssetType Type;
		};

		// .dds is deliberately absent: nothing here decodes a DDS container, so advertising it registered
		// an asset whose every load failed, and a rescan re-registered it after each manual cleanup.
		constexpr std::array kRows = {
		    ExtensionRow{".obj", AssetType::Mesh},
		    ExtensionRow{".fbx", AssetType::Mesh},
		    ExtensionRow{".gltf", AssetType::Mesh},
		    ExtensionRow{".glb", AssetType::Mesh},
		    // What stb_image decodes.
		    ExtensionRow{".png", AssetType::Texture},
		    ExtensionRow{".jpg", AssetType::Texture},
		    ExtensionRow{".jpeg", AssetType::Texture},
		    ExtensionRow{".tga", AssetType::Texture},
		    ExtensionRow{".bmp", AssetType::Texture},
		    ExtensionRow{".ssmat", AssetType::Material},
		    ExtensionRow{".hlsl", AssetType::Shader},
		    ExtensionRow{".world", AssetType::Scene},
		    // What miniaudio decodes without an extra decoder dependency.
		    ExtensionRow{".wav", AssetType::Audio},
		    ExtensionRow{".flac", AssetType::Audio},
		    ExtensionRow{".mp3", AssetType::Audio},
		};

		constexpr std::array<const char*, kRows.size()> MakeExtensions()
		{
			std::array<const char*, kRows.size()> out{};
			for (size_t i = 0; i < kRows.size(); ++i)
			{
				out[i] = kRows[i].Ext;
			}
			return out;
		}

		constexpr auto kExtensions = MakeExtensions();

		bool SameContent(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			std::error_code ec;
			if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec)
			{
				return false;
			}
			std::ifstream fa(a, std::ios::binary);
			std::ifstream fb(b, std::ios::binary);
			return fa && fb &&
			       std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
			                  std::istreambuf_iterator<char>(fb));
		}
	} // namespace

	AssetType AssetTypeFromExtension(const std::string_view ext)
	{
		for (const ExtensionRow& row : kRows)
		{
			const std::string_view candidate = row.Ext;
			if (candidate.size() == ext.size() &&
			    std::equal(ext.begin(), ext.end(), candidate.begin(), [](const char x, const char y)
			               { return std::tolower(static_cast<unsigned char>(x)) == y; }))
			{
				return row.Type;
			}
		}
		return AssetType::None;
	}

	AssetType AssetTypeFromPath(const std::filesystem::path& path)
	{
		return AssetTypeFromExtension(path.extension().string());
	}

	std::span<const char* const> ImportableExtensions()
	{
		return kExtensions;
	}

	std::filesystem::path DefaultImportFolder(const AssetType type)
	{
		switch (type)
		{
		case AssetType::Mesh:
			return "meshes";
		case AssetType::Texture:
			return "textures";
		case AssetType::Material:
			return "materials";
		case AssetType::Shader:
			return "shaders";
		case AssetType::Audio:
			return "audio";
		default:
			return {};
		}
	}

	ImportPlan PlanImport(const std::filesystem::path& source, const std::filesystem::path& projectDir,
	                      const std::filesystem::path& destFolderRel)
	{
		ImportPlan plan;
		plan.DestRel = source;
		plan.DestAbs = source;
		if (projectDir.empty())
		{
			return plan;
		}

		std::error_code ec;
		const std::filesystem::path projectCanon = std::filesystem::weakly_canonical(projectDir, ec);
		const std::filesystem::path sourceCanon = std::filesystem::weakly_canonical(std::filesystem::absolute(source, ec), ec);
		const std::filesystem::path rel = sourceCanon.lexically_relative(projectCanon);
		if (!rel.empty() && *rel.begin() != "..")
		{
			plan.DestRel = rel;
			plan.DestAbs = sourceCanon;
			return plan;
		}

		plan.Copy = true;
		const std::string stem = source.stem().string();
		const std::string ext = source.extension().string();
		std::filesystem::path name = source.filename();
		for (int i = 1;; ++i)
		{
			const std::filesystem::path abs = projectCanon / destFolderRel / name;
			if (!std::filesystem::exists(abs, ec))
			{
				break;
			}
			if (SameContent(source, abs))
			{
				plan.ReuseExisting = true;
				break;
			}
			plan.Renamed = true;
			name = stem + "_" + std::to_string(i) + ext;
		}
		plan.DestRel = destFolderRel / name;
		plan.DestAbs = projectCanon / plan.DestRel;
		return plan;
	}

	DropPlan PlanDrop(const std::span<const std::filesystem::path> dropped, const bool overTarget)
	{
		DropPlan plan;
		if (!overTarget)
		{
			return plan;
		}
		for (const std::filesystem::path& path : dropped)
		{
			const AssetType type = AssetTypeFromPath(path);
			if (type == AssetType::None || type == AssetType::Scene)
			{
				++plan.Refused;
				continue;
			}
			plan.Items.push_back({path, type, DefaultImportFolder(type)});
		}
		return plan;
	}
} // namespace Snowstorm
