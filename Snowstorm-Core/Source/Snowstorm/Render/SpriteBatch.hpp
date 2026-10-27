#pragma once

#include "Snowstorm/Math/Math.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// MUST match SpriteInstance in Sprite.vert.hlsl field for field (std430).
	struct SpriteInstance
	{
		// Maps the unit quad [0,1]^2 to world space with size, pivot and flips baked in.
		glm::mat4 Model{1.0f};
		glm::vec4 Tint{1.0f};
		uint32_t TextureIndex = 0;
		uint32_t Flags = 0;
		uint32_t _Pad0 = 0;
		uint32_t _Pad1 = 0;
		glm::vec4 UvRect{0.0f, 0.0f, 1.0f, 1.0f}; // xy offset, zw scale, in normalized texture space
	};
	static_assert(sizeof(SpriteInstance) == 112, "SpriteInstance layout must match Sprite.vert.hlsl");

	inline constexpr uint32_t kSpriteFlagLinear = 1u;

	// Texel rect (x, y, w, h; origin top-left, the order rows are stored) to the UV offset and scale the vertex
	// stage applies to the unit quad. A zero width or height selects the whole texture. The rect clamps to the
	// texture, and a rect with no area left, or a texture of unknown size, falls back to the whole texture too.
	[[nodiscard]] inline glm::vec4 SpriteUvRect(const glm::vec4& texelRect, const uint32_t texWidth, const uint32_t texHeight)
	{
		constexpr glm::vec4 kWhole{0.0f, 0.0f, 1.0f, 1.0f};
		if (texelRect.z <= 0.0f || texelRect.w <= 0.0f || texWidth == 0 || texHeight == 0)
			return kWhole;
		const float w = static_cast<float>(texWidth);
		const float h = static_cast<float>(texHeight);
		const float x0 = std::clamp(texelRect.x, 0.0f, w);
		const float y0 = std::clamp(texelRect.y, 0.0f, h);
		const float x1 = std::clamp(texelRect.x + texelRect.z, 0.0f, w);
		const float y1 = std::clamp(texelRect.y + texelRect.w, 0.0f, h);
		if (x1 <= x0 || y1 <= y0)
			return kWhole;
		return {x0 / w, y0 / h, (x1 - x0) / w, (y1 - y0) / h};
	}

	struct SpriteDraw
	{
		glm::mat4 Transform{1.0f};
		glm::vec2 Size{1.0f};
		glm::vec2 Pivot{0.5f};
		glm::vec4 Tint{1.0f};
		glm::vec4 UvRect{0.0f, 0.0f, 1.0f, 1.0f};
		bool FlipX = false;
		bool FlipY = false;
		bool Linear = false;
		int32_t Layer = 0;
		int32_t Order = 0;
		uint32_t Sequence = 0; // final tie-break, so equal (Layer, Order) keeps a stable order across frames
		uint32_t TextureIndex = 0;
	};

	[[nodiscard]] inline glm::mat4 SpriteModelMatrix(const SpriteDraw& d)
	{
		// A flip mirrors about the pivot, as Unity's SpriteRenderer does, so a flipped sprite stays anchored.
		const glm::vec2 flip{d.FlipX ? -1.0f : 1.0f, d.FlipY ? -1.0f : 1.0f};
		glm::mat4 local = glm::scale(glm::mat4(1.0f), glm::vec3(d.Size * flip, 1.0f));
		local = glm::translate(local, glm::vec3(-d.Pivot, 0.0f));
		return d.Transform * local;
	}

	// Back to front: the first instance is drawn first, so it ends up underneath.
	inline void SortSpriteDraws(std::vector<SpriteDraw>& draws)
	{
		std::sort(draws.begin(), draws.end(), [](const SpriteDraw& a, const SpriteDraw& b)
		          {
			          if (a.Layer != b.Layer)
				          return a.Layer < b.Layer;
			          if (a.Order != b.Order)
				          return a.Order < b.Order;
			          return a.Sequence < b.Sequence; });
	}

	[[nodiscard]] inline std::vector<SpriteInstance> BuildSpriteInstances(std::vector<SpriteDraw>& draws)
	{
		SortSpriteDraws(draws);
		std::vector<SpriteInstance> out;
		out.reserve(draws.size());
		for (const SpriteDraw& d : draws)
		{
			SpriteInstance inst;
			inst.Model = SpriteModelMatrix(d);
			inst.Tint = d.Tint;
			inst.UvRect = d.UvRect;
			inst.TextureIndex = d.TextureIndex;
			inst.Flags = d.Linear ? kSpriteFlagLinear : 0u;
			out.push_back(inst);
		}
		return out;
	}
}
