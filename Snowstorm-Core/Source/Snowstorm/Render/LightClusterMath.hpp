#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// View space convention: +z is distance along the view direction (positive depth), x right, y up.
	struct ClusterGrid
	{
		uint32_t TilePx = 64;
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t Slices = 24;
		float Near = 0.1f;
		float Far = 100.0f;

		uint32_t TilesX() const { return (Width + TilePx - 1) / TilePx; }
		uint32_t TilesY() const { return (Height + TilePx - 1) / TilePx; }
		uint32_t ClusterCount() const { return TilesX() * TilesY() * Slices; }
		uint32_t Index(uint32_t x, uint32_t y, uint32_t slice) const { return (slice * TilesY() + y) * TilesX() + x; }
	};

	struct ClusterAabb
	{
		float Min[3];
		float Max[3];
	};

	struct ClusterSphere
	{
		float Center[3];
		float Radius;
	};

	// Exponential slicing (Olsson et al. 2012). Depths outside [Near, Far] clamp to the first/last slice.
	inline uint32_t ClusterSliceFromDepth(const ClusterGrid& g, float depth)
	{
		if (!(depth > g.Near))
			return 0;
		const float s = std::log(depth / g.Near) * static_cast<float>(g.Slices) / std::log(g.Far / g.Near);
		return std::min(static_cast<uint32_t>(s), g.Slices - 1);
	}

	// Depth of the near face of slice k; k == Slices gives Far.
	inline float ClusterSliceDepth(const ClusterGrid& g, uint32_t k)
	{
		return g.Near * std::pow(g.Far / g.Near, static_cast<float>(k) / static_cast<float>(g.Slices));
	}

	inline uint32_t ClusterTileFromPixel(const ClusterGrid& g, uint32_t px)
	{
		return px / g.TilePx;
	}

	// View-space bounds of cluster (x, y, slice) for a symmetric perspective projection. The last tile row/column
	// is clipped to the render target, so the tiles cover the screen exactly with no overhang.
	inline ClusterAabb ClusterBounds(const ClusterGrid& g, uint32_t x, uint32_t y, uint32_t slice, float tanHalfFovY, float aspect)
	{
		const float x0 = static_cast<float>(x * g.TilePx);
		const float x1 = static_cast<float>(std::min((x + 1) * g.TilePx, g.Width));
		const float y0 = static_cast<float>(y * g.TilePx);
		const float y1 = static_cast<float>(std::min((y + 1) * g.TilePx, g.Height));

		// Pixel row 0 is the top of the image, so y flips into view-space up.
		const float ndcX0 = 2.0f * x0 / static_cast<float>(g.Width) - 1.0f;
		const float ndcX1 = 2.0f * x1 / static_cast<float>(g.Width) - 1.0f;
		const float ndcYTop = 1.0f - 2.0f * y0 / static_cast<float>(g.Height);
		const float ndcYBot = 1.0f - 2.0f * y1 / static_cast<float>(g.Height);

		const float zn = ClusterSliceDepth(g, slice);
		const float zf = ClusterSliceDepth(g, slice + 1);
		const float tx = tanHalfFovY * aspect;

		ClusterAabb b{{1e30f, 1e30f, zn}, {-1e30f, -1e30f, zf}};
		for (float z : {zn, zf})
		{
			for (float nx : {ndcX0, ndcX1})
			{
				const float vx = nx * tx * z;
				b.Min[0] = std::min(b.Min[0], vx);
				b.Max[0] = std::max(b.Max[0], vx);
			}
			for (float ny : {ndcYBot, ndcYTop})
			{
				const float vy = ny * tanHalfFovY * z;
				b.Min[1] = std::min(b.Min[1], vy);
				b.Max[1] = std::max(b.Max[1], vy);
			}
		}
		return b;
	}

	// Touching counts as overlapping, so a cull built on it is conservative.
	inline bool SphereOverlapsAabb(const ClusterSphere& s, const ClusterAabb& b)
	{
		float d2 = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			const float v = s.Center[i];
			const float e = std::max(b.Min[i] - v, 0.0f) + std::max(v - b.Max[i], 0.0f);
			d2 += e * e;
		}
		return d2 <= s.Radius * s.Radius;
	}

	// CPU reference of the planned compute cull: ascending light index, fixed stride, overflow dropped.
	// Returns the lights overlapping the cluster, at most maxPerCluster.
	inline std::vector<uint32_t> CullClusterLights(const ClusterAabb& cluster, const std::vector<ClusterSphere>& lights, uint32_t maxPerCluster)
	{
		std::vector<uint32_t> out;
		for (uint32_t i = 0; i < lights.size() && out.size() < maxPerCluster; ++i)
			if (SphereOverlapsAabb(lights[i], cluster))
				out.push_back(i);
		return out;
	}

	// Per-cluster light lists with a fixed stride, the layout the planned GPU buffer takes. Indices of cluster c
	// live at [c * Stride, c * Stride + Counts[c]).
	struct ClusterLightLists
	{
		uint32_t Stride = 0;
		std::vector<uint32_t> Counts;
		std::vector<uint32_t> Indices;
		uint32_t Overflowed = 0; // clusters where at least one overlapping light was dropped
		uint32_t Occupied = 0;   // clusters with at least one light
		uint32_t MaxCount = 0;
	};

	// Flat GPU image of the lists: Counts[ClusterCount] then Indices[ClusterCount * Stride]. Cluster c's list
	// starts at word ClusterCount + c * Stride.
	inline std::vector<uint32_t> PackClusterLightLists(const ClusterLightLists& lists)
	{
		std::vector<uint32_t> words;
		words.reserve(lists.Counts.size() + lists.Indices.size());
		words.insert(words.end(), lists.Counts.begin(), lists.Counts.end());
		words.insert(words.end(), lists.Indices.begin(), lists.Indices.end());
		return words;
	}

	// Cluster of a fragment: pixel (px, py) and positive view depth. DefaultLit.frag.hlsl repeats this
	// arithmetic (ClusterIndexOf) with SliceScale = Slices / log(Far / Near); keep the two in step.
	inline uint32_t ClusterIndexFromPixel(const ClusterGrid& g, uint32_t px, uint32_t py, float depth)
	{
		const uint32_t x = std::min(ClusterTileFromPixel(g, px), g.TilesX() - 1);
		const uint32_t y = std::min(ClusterTileFromPixel(g, py), g.TilesY() - 1);
		return g.Index(x, y, ClusterSliceFromDepth(g, depth));
	}

	// Lights are view-space spheres (see the convention on ClusterGrid). Brute force O(clusters x lights): the
	// CPU reference for the compute cull, not a per-frame production path.
	inline ClusterLightLists AssignClusterLights(const ClusterGrid& g, float tanHalfFovY, float aspect, const std::vector<ClusterSphere>& lights, uint32_t stride)
	{
		ClusterLightLists out;
		out.Stride = stride;
		out.Counts.assign(g.ClusterCount(), 0);
		out.Indices.assign(static_cast<size_t>(g.ClusterCount()) * stride, 0);
		for (uint32_t s = 0; s < g.Slices; ++s)
			for (uint32_t y = 0; y < g.TilesY(); ++y)
				for (uint32_t x = 0; x < g.TilesX(); ++x)
				{
					const ClusterAabb b = ClusterBounds(g, x, y, s, tanHalfFovY, aspect);
					const uint32_t c = g.Index(x, y, s);
					uint32_t n = 0;
					bool dropped = false;
					for (uint32_t i = 0; i < lights.size(); ++i)
					{
						if (!SphereOverlapsAabb(lights[i], b))
							continue;
						if (n < stride)
							out.Indices[static_cast<size_t>(c) * stride + n++] = i;
						else
							dropped = true;
					}
					out.Counts[c] = n;
					out.Occupied += n > 0 ? 1u : 0u;
					out.Overflowed += dropped ? 1u : 0u;
					out.MaxCount = std::max(out.MaxCount, n);
				}
		return out;
	}
}
