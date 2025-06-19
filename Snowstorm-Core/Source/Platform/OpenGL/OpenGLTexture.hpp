#pragma once

#include "Snowstorm/Render/Texture.hpp"

#include <GL/glew.h>

namespace Snowstorm
{
	class OpenGLTexture2D final : public Texture2D
	{
	public:
		OpenGLTexture2D(uint32_t width, uint32_t height);
		explicit OpenGLTexture2D(std::string path);
		~OpenGLTexture2D() override;

		OpenGLTexture2D(const OpenGLTexture2D& other) = delete;
		OpenGLTexture2D(OpenGLTexture2D&& other) = delete;
		OpenGLTexture2D& operator=(const OpenGLTexture2D& other) = delete;
		OpenGLTexture2D& operator=(OpenGLTexture2D&& other) = delete;

		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }

		uint32_t GetRendererID() const override { return m_RendererID; }

		void SetData(void* data, uint32_t size) override;

		void Bind(uint32_t slot = 0) const override;

		bool operator==(const Texture& other) const override
		{
			return m_RendererID == dynamic_cast<const OpenGLTexture2D&>(other).m_RendererID;
		}

	private:
		std::string m_Path;
		uint32_t m_Width, m_Height;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
	};

	class OpenGLTextureCube final : public TextureCube
	{
	public:
		explicit OpenGLTextureCube(const std::array<std::string, 6>& faces);
		explicit OpenGLTextureCube(const std::string& path);
		~OpenGLTextureCube() override;

		OpenGLTextureCube(const OpenGLTextureCube&) = delete;
		OpenGLTextureCube(OpenGLTextureCube&&) = delete;
		OpenGLTextureCube& operator=(const OpenGLTextureCube&) = delete;
		OpenGLTextureCube& operator=(OpenGLTextureCube&&) = delete;

		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }
		uint32_t GetRendererID() const override { return m_RendererID; }

		void SetData(void* data, uint32_t size) override {} // not used
		void Bind(uint32_t slot = 0) const override;

		bool operator==(const Texture& other) const override
		{
			return m_RendererID == other.GetRendererID();
		}

	private:
		uint32_t m_RendererID{0};
		uint32_t m_Width{0}, m_Height{0};
	};
}
