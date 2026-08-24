#include "Shader.hpp"

#include "Platform/Vulkan/VulkanShader.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "RendererAPI.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace Snowstorm
{
	namespace
	{
		// Library key = source path(s), then the feature defines after a '#'. Mirrors the existing "vert|frag"
		// composite: a key is parsed back to its file(s) by stripping the suffix, never stored separately.
		std::string MakeShaderKey(const std::string& base, const ShaderDefines& features)
		{
			if (features.empty())
			{
				return base; // unpermuted call sites keep their original key, so nothing re-keys
			}
			std::string key = base + "#";
			for (size_t i = 0; i < features.size(); ++i)
			{
				if (i != 0)
				{
					key += ',';
				}
				key += features[i];
			}
			return key;
		}

		// The file-path portion of a library key: everything before the '#' feature suffix.
		std::string_view SourcePartOfKey(const std::string& key)
		{
			return std::string_view(key).substr(0, key.find('#'));
		}
	}

	Ref<Shader> Shader::Create(const std::string& filepath, ShaderDefines features)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL is not supported by this build/config.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanShader>(filepath, std::move(features));

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 shader backend not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<Shader> Shader::Create(const std::string& vertPath, const std::string& fragPath, ShaderDefines features)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL is not supported by this build/config.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanShader>(vertPath, fragPath, std::move(features));

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 shader backend not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader, const std::string& filepath)
	{
		SS_CORE_ASSERT(!Exists(filepath), "Shader already exists!");
		m_Shaders[filepath] = shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& filepath, ShaderDefines features)
	{
		const std::string key = MakeShaderKey(filepath, features);
		if (Exists(key))
		{
			return Get(key);
		}

		auto shader = Shader::Create(filepath, std::move(features));
		Add(shader, key);
		SubmitAsyncCompile(shader);

		m_LastModifications[key] = std::filesystem::last_write_time(filepath);

		return shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& vertPath, const std::string& fragPath, ShaderDefines features)
	{
		// Key on the composite so a (vert, frag) pair is one library entry; hot-reload watches the newer
		// of the two files (editing either re-triggers). See ReloadAll's composite-key handling.
		const std::string key = MakeShaderKey(vertPath + "|" + fragPath, features);
		if (Exists(key))
		{
			return Get(key);
		}

		auto shader = Shader::Create(vertPath, fragPath, std::move(features));
		Add(shader, key);
		SubmitAsyncCompile(shader);

		m_LastModifications[key] = std::max(std::filesystem::last_write_time(vertPath),
		                                    std::filesystem::last_write_time(fragPath));

		return shader;
	}

	void ShaderLibrary::SubmitAsyncCompile(const Ref<Shader>& shader)
	{
		// Compile off the main thread so a cold cache (dxc.exe spawn per stage, seconds total) doesn't
		// block the frame loop — the editor keeps presenting chrome + sky + a progress bar while shaders
		// build, and each material's pipeline is created once its shader reports ready (see
		// AssetManagerSingleton::GetOrCreatePipeline). Warm-cache "compiles" are near-instant fs::exists
		// checks that still run on the worker (harmless).
		// Reset the high-water total when starting a fresh batch (nothing was in flight), so the bar reads
		// e.g. "2/11" per cold start rather than accumulating across the app's lifetime.
		if (m_PendingCompiles.load(std::memory_order_relaxed) == 0)
		{
			m_PendingCompileTotal = 0;
		}
		m_PendingCompiles.fetch_add(1, std::memory_order_relaxed);
		++m_PendingCompileTotal;

		// No JobSystem (e.g. a headless unit-test context without an Application) -> compile synchronously so
		// behaviour degrades safely instead of never becoming ready.
		if (!Application::Get().GetServiceManager().ServiceRegistered<JobSystem>())
		{
			shader->Recompile();
			m_PendingCompiles.fetch_sub(1, std::memory_order_relaxed);
			return;
		}

		auto& jobs = Application::Get().GetServiceManager().GetService<JobSystem>();
		// Capture the Ref by value so the shader stays alive until the compile finishes even if the library
		// entry is replaced. Recompile() is thread-safe (see VulkanShader::Compile).
		(void)jobs.Submit([this, shader]
		                  {
			shader->Recompile();
			m_PendingCompiles.fetch_sub(1, std::memory_order_relaxed); });
	}

	Ref<Shader> ShaderLibrary::Get(const std::string& filepath)
	{
		SS_CORE_ASSERT(Exists(filepath), "Shader not found!");
		return m_Shaders[filepath];
	}

	bool ShaderLibrary::Exists(const std::string& filepath) const
	{
		return m_Shaders.contains(filepath);
	}

	void ShaderLibrary::ReloadAll()
	{
		for (auto& [key, lastModified] : m_LastModifications)
		{
			// A key is the source path(s) plus an optional '#' feature suffix, and the path part may itself
			// be a composite "vert|frag". Strip the suffix, then take the newest mtime across the constituent
			// file(s) so editing either stage triggers a recompile. Two permutations of one file are separate
			// entries that both recompile, which is what hot-reloading that file should do.
			const std::string paths(SourcePartOfKey(key));
			std::filesystem::file_time_type newest{};
			const size_t sep = paths.find('|');
			if (sep == std::string::npos)
			{
				newest = std::filesystem::last_write_time(paths);
			}
			else
			{
				newest = std::max(std::filesystem::last_write_time(paths.substr(0, sep)),
				                  std::filesystem::last_write_time(paths.substr(sep + 1)));
			}

			if (newest > lastModified)
			{
				Get(key)->Recompile();
				lastModified = newest;
			}
		}
	}

}
