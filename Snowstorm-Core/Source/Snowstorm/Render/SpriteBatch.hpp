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
	};
	static_assert(sizeof(SpriteInstance) == 96, "SpriteInstance layout must match Sprite.vert.hlsl");

	inline constexpr uint32_t kSpriteFlagLinear = 1u;

	struct SpriteDraw
	{
		glm::mat4 Transform{1.0f};
		glm::vec2 Size{1.0f};
		glm::vec2 Pivot{0.5f};
		glm::vec4 Tint{1.0f};
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
			inst.TextureIndex = d.TextureIndex;
			inst.Flags = d.Linear ? kSpriteFlagLinear : 0u;
			out.push_back(inst);
		}
		return out;
	}
}
