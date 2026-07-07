#include "Shader.hpp"

#include "Platform/Vulkan/VulkanShader.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "RendererAPI.hpp"

#include <algorithm>
#include <filesystem>

namespace Snowstorm
{
	Ref<Shader> Shader::Create(const std::string& filepath)
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
			return CreateRef<VulkanShader>(filepath);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 shader backend not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<Shader> Shader::Create(const std::string& vertPath, const std::string& fragPath)
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
			return CreateRef<VulkanShader>(vertPath, fragPath);

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

	Ref<Shader> ShaderLibrary::Load(const std::string& filepath)
	{
		if (Exists(filepath))
		{
			return Get(filepath);
		}

		auto shader = Shader::Create(filepath);
		Add(shader, filepath);
		SubmitAsyncCompile(shader);

		m_LastModifications[filepath] = std::filesystem::last_write_time(filepath);

		return shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& vertPath, const std::string& fragPath)
	{
		// Key on the composite so a (vert, frag) pair is one library entry; hot-reload watches the newer
		// of the two files (editing either re-triggers). See ReloadAll's composite-key handling.
		const std::string key = vertPath + "|" + fragPath;
		if (Exists(key))
		{
			return Get(key);
		}

		auto shader = Shader::Create(vertPath, fragPath);
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
			// A key may be a single path or a composite "vert|frag" (two-path graphics shader). Take the
			// newest mtime across its constituent file(s) so editing either stage triggers a recompile.
			std::filesystem::file_time_type newest{};
			const size_t sep = key.find('|');
			if (sep == std::string::npos)
			{
				newest = std::filesystem::last_write_time(key);
			}
			else
			{
				newest = std::max(std::filesystem::last_write_time(key.substr(0, sep)),
				                  std::filesystem::last_write_time(key.substr(sep + 1)));
			}

			if (newest > lastModified)
			{
				Get(key)->Recompile();
				lastModified = newest;
			}
		}
	}
}
