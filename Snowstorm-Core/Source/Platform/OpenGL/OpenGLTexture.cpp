#include "pch.h"
#include "OpenGLTexture.hpp"

// TODO move this macro somewhere else
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <GL/glew.h>
#include <gli/gli.hpp>

namespace
{
	void GetGLFormatFromGli(const gli::format format, GLenum& internalFormat, GLenum& formatGL, GLenum& type)
	{
		switch (format)
		{
		case gli::FORMAT_RGBA8_UNORM_PACK8:
			internalFormat = GL_RGBA8;
			formatGL = GL_RGBA;
			type = GL_UNSIGNED_BYTE;
			break;

		case gli::FORMAT_RGB8_UNORM_PACK8:
			internalFormat = GL_RGB8;
			formatGL = GL_RGB;
			type = GL_UNSIGNED_BYTE;
			break;

		case gli::FORMAT_RGBA16_SFLOAT_PACK16:
			internalFormat = GL_RGBA16F;
			formatGL = GL_RGBA;
			type = GL_HALF_FLOAT;
			break;

		case gli::FORMAT_RGBA32_SFLOAT_PACK32:
			internalFormat = GL_RGBA32F;
			formatGL = GL_RGBA;
			type = GL_FLOAT;
			break;

		case gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8:
			internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			formatGL = GL_RGBA; // ignored
			type = 0;
			break;

		default:
			SS_CORE_WARN("Unsupported GLI format: fallback to RGBA8");
			internalFormat = GL_RGBA8;
			formatGL = GL_RGBA;
			type = GL_UNSIGNED_BYTE;
			break;
		}
	}
}

namespace Snowstorm
{
	OpenGLTexture2D::OpenGLTexture2D(const uint32_t width, const uint32_t height)
		: m_Width(width), m_Height(height)
	{
		SS_PROFILE_FUNCTION();

		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	OpenGLTexture2D::OpenGLTexture2D(std::string path)
		: m_Path(std::move(path))
	{
		SS_PROFILE_FUNCTION();

		int width, height, channels;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = nullptr;
		{
			SS_PROFILE_SCOPE("stbi_load - OpenGLTexture2D::OpenGLTexture2D(const std::string&)");
			data = stbi_load(m_Path.c_str(), &width, &height, &channels, 0);
		}
		SS_CORE_ASSERT(data, "Failed to load image!");
		m_Width = width;
		m_Height = height;

		GLenum internalFormat = 0, dataFormat = 0;
		if (channels == 4)
		{
			internalFormat = GL_RGBA8;
			dataFormat = GL_RGBA;
		}
		else if (channels == 3)
		{
			internalFormat = GL_RGB8;
			dataFormat = GL_RGB;
		}

		m_InternalFormat = internalFormat;
		m_DataFormat = dataFormat;

		SS_CORE_ASSERT(internalFormat & dataFormat, "Format not supported!");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, dataFormat, GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		SS_PROFILE_FUNCTION();

		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture2D::SetData(void* data, const uint32_t size)
	{
		SS_PROFILE_FUNCTION();

		const uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
		SS_CORE_ASSERT(size == m_Width * m_Height * bpp, "Data must be entire texture!");
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	void OpenGLTexture2D::Bind(const uint32_t slot) const
	{
		SS_PROFILE_FUNCTION();

		glBindTextureUnit(slot, m_RendererID);
	}

	//----------------------------------------------------------------------------------------------------------------------------------------
	OpenGLTextureCube::OpenGLTextureCube(const std::array<std::string, 6>& faces)
	{
		// TODO implement this
	}

	OpenGLTextureCube::OpenGLTextureCube(const std::string& path)
	{
		SS_PROFILE_FUNCTION();

		gli::texture tex = gli::load(path);
		if (tex.empty())
		{
			SS_CORE_ERROR("Failed to load cubemap: {}", path);
			return;
		}

		if (tex.target() != gli::TARGET_CUBE)
		{
			SS_CORE_ERROR("Texture is not a cubemap: {}", path);
			return;
		}

		gli::texture_cube texCube(tex);

		GLenum internalFormat = 0, formatGL = 0, type = 0;
		GetGLFormatFromGli(tex.format(), internalFormat, formatGL, type);

		if (internalFormat == GL_NONE || formatGL == GL_NONE)
		{
			SS_CORE_ERROR("Unsupported texture format: {}", static_cast<int>(tex.format()));
			return;
		}

		m_Width = static_cast<uint32_t>(tex.extent().x);
		m_Height = static_cast<uint32_t>(tex.extent().y);

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

		for (uint32_t face = 0; face < 6; ++face)
		{
			for (uint32_t level = 0; level < texCube.levels(); ++level)
			{
				glm::ivec2 extent = texCube[face].extent(level);
				GLsizei width = static_cast<GLsizei>(extent.x);
				GLsizei height = static_cast<GLsizei>(extent.y);
				const void* data = texCube[face].data(0, 0, level);
				GLsizei size = static_cast<GLsizei>(texCube[face].size(level));

				if (gli::is_compressed(tex.format()))
				{
					glCompressedTexImage2D(
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
						static_cast<GLint>(level),
						internalFormat,
						width,
						height,
						0,
						size,
						data
					);
				}
				else
				{
					glTexImage2D(
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
						static_cast<GLint>(level),
						internalFormat,
						width,
						height,
						0,
						formatGL,
						type,
						data
					);
				}
			}
		}

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, texCube.levels() - 1);
	}

	OpenGLTextureCube::~OpenGLTextureCube()
	{
		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTextureCube::Bind(const uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}
}
