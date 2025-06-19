#include "pch.h"

#include "VulkanTexture.h"

namespace Snowstorm
{
	// TODO implement all of this
	VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
	{
	}

	VulkanTexture2D::VulkanTexture2D(std::string path)
	{
	}

	VulkanTexture2D::~VulkanTexture2D()
	{
	}

	uint32_t VulkanTexture2D::GetWidth() const
	{
		return 0;
	}

	uint32_t VulkanTexture2D::GetHeight() const
	{
		return 0;
	}

	uint32_t VulkanTexture2D::GetRendererID() const
	{
		return 0;
	}

	void VulkanTexture2D::SetData(void* data, uint32_t size)
	{
	}

	void VulkanTexture2D::Bind(uint32_t slot) const
	{
	}

	bool VulkanTexture2D::operator==(const Texture& other) const
	{
		return true;
	}

	VulkanTextureCube::VulkanTextureCube(const std::array<std::string, 6>& faces)
	{
	}

	VulkanTextureCube::VulkanTextureCube(const std::string& path)
	{
	}

	VulkanTextureCube::~VulkanTextureCube()
	{
	}

	uint32_t VulkanTextureCube::GetWidth() const
	{
		return 0;
	}

	uint32_t VulkanTextureCube::GetHeight() const
	{
		return 0;
	}

	uint32_t VulkanTextureCube::GetRendererID() const
	{
		return 0;
	}

	void VulkanTextureCube::SetData(void* data, uint32_t size)
	{
	}

	void VulkanTextureCube::Bind(uint32_t slot) const
	{
	}

	bool VulkanTextureCube::operator==(const Texture& other) const
	{
		return true;
	}
}
