#include "Shader.hpp"

#include "Platform/Vulkan/VulkanShader.hpp"

#include "Snowstorm/Core/Log.hpp"

#include "RendererAPI.hpp"

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

	void ShaderLibrarySingleton::Add(const Ref<Shader>& shader, const std::string& filepath)
	{
		SS_CORE_ASSERT(!Exists(filepath), "Shader already exists!");
		m_Shaders[filepath] = shader;
	}

	Ref<Shader> ShaderLibrarySingleton::Load(const std::string& filepath)
	{
		if (Exists(filepath))
		{
			return Get(filepath);
		}

		auto shader = Shader::Create(filepath);
		Add(shader, filepath);

		m_LastModifications[filepath] = std::filesystem::last_write_time(filepath);

		return shader;
	}

	Ref<Shader> ShaderLibrarySingleton::Get(const std::string& filepath)
	{
		SS_CORE_ASSERT(Exists(filepath), "Shader not found!");
		return m_Shaders[filepath];
	}

	bool ShaderLibrarySingleton::Exists(const std::string& filepath) const
	{
		return m_Shaders.contains(filepath);
	}

	void ShaderLibrarySingleton::ReloadAll()
	{
		for (const auto& [filepath, lastModified] : m_LastModifications)
		{
			if (std::filesystem::last_write_time(filepath) > lastModified)
			{
				Get(filepath)->Recompile();
				m_LastModifications[filepath] = std::filesystem::last_write_time(filepath);
			}
		}
	}
}
