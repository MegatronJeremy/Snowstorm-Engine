#pragma once

#include <string>

#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	class Texture
	{
	public:
		Texture() = default;
		virtual ~Texture() = default;

		Texture(const Texture& other) = delete;
		Texture(Texture&& other) = delete;
		Texture& operator=(const Texture& other) = delete;
		Texture& operator=(Texture&& other) = delete;

		[[nodiscard]] virtual uint32_t GetWidth() const = 0;
		[[nodiscard]] virtual uint32_t GetHeight() const = 0;

		[[nodiscard]] virtual uint32_t GetRendererID() const = 0;

		virtual void SetData(void* data, uint32_t size) = 0;

		virtual void Bind(uint32_t slot = 0) const = 0;

		virtual bool operator==(const Texture& other) const = 0;
	};

	class Texture2D : public Texture
	{
	public:
		static Ref<Texture2D> Create(uint32_t width, uint32_t height);
		static Ref<Texture2D> Create(const std::string& path);
	};

	class TextureCube : public Texture
	{
	public:
		static Ref<TextureCube> Create(const std::array<std::string, 6>& paths);
		static Ref<TextureCube> Create(const std::string& path);
	};
}
