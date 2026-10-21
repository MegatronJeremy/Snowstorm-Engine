#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Snowstorm::CascadeMath
{
	constexpr uint32_t kMaxCascades = 4;

	struct Sphere
	{
		glm::vec3 Center{0.0f};
		float Radius = 0.0f;
	};

	// Practical split scheme (Zhang PSSM, GPU Gems 3 ch. 10): lambda 0 is uniform, 1 is logarithmic.
	// Entry i is the view-space far distance of cascade i; the last entry is exactly `farZ`.
	inline std::array<float, kMaxCascades> ComputeSplits(float nearZ, float farZ, uint32_t count, float lambda)
	{
		std::array<float, kMaxCascades> splits{};
		count = glm::clamp(count, 1u, kMaxCascades);
		for (uint32_t i = 0; i < count; ++i)
		{
			const float t = static_cast<float>(i + 1) / static_cast<float>(count);
			const float logSplit = nearZ * std::pow(farZ / nearZ, t);
			const float uniSplit = nearZ + (farZ - nearZ) * t;
			splits[i] = (i + 1 == count) ? farZ : glm::mix(uniSplit, logSplit, lambda);
		}
		for (uint32_t i = count; i < kMaxCascades; ++i)
		{
			splits[i] = farZ;
		}
		return splits;
	}

	// The 8 world-space corners of the camera frustum between view depths `sliceNear` and `sliceFar`.
	// `invViewProj` inverts a right-handed, zero-to-one depth projection with the given near/far; view depth is
	// linear along each corner ray, so slices interpolate between the near and far plane corners.
	inline std::array<glm::vec3, 8> SliceCorners(const glm::mat4& invViewProj, float camNear, float camFar,
	                                             float sliceNear, float sliceFar)
	{
		std::array<glm::vec3, 8> out{};
		const float span = camFar - camNear;
		const float t0 = (sliceNear - camNear) / span;
		const float t1 = (sliceFar - camNear) / span;
		uint32_t k = 0;
		for (int y = -1; y <= 1; y += 2)
		{
			for (int x = -1; x <= 1; x += 2)
			{
				glm::vec4 n = invViewProj * glm::vec4(static_cast<float>(x), static_cast<float>(y), 0.0f, 1.0f);
				glm::vec4 f = invViewProj * glm::vec4(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f);
				const glm::vec3 np = glm::vec3(n) / n.w;
				const glm::vec3 fp = glm::vec3(f) / f.w;
				out[k] = glm::mix(np, fp, t0);
				out[k + 4] = glm::mix(np, fp, t1);
				++k;
			}
		}
		return out;
	}

	// Centroid-centred sphere. Its radius does not change when the camera rotates, which is what keeps the
	// shadow texel size constant. The radius is rounded up to `radiusStep` so it does not breathe.
	inline Sphere FitSphere(const std::array<glm::vec3, 8>& corners, float radiusStep = 1.0f / 16.0f)
	{
		glm::vec3 c(0.0f);
		for (const glm::vec3& p : corners)
		{
			c += p;
		}
		c /= 8.0f;
		float r = 0.0f;
		for (const glm::vec3& p : corners)
		{
			r = glm::max(r, glm::length(p - c));
		}
		return {c, std::ceil(r / radiusStep) * radiusStep};
	}

	// Ortho light view-projection for one cascade. The slice centre is snapped to whole shadow texels in light
	// space, so camera translation below one texel leaves the matrix unchanged. `casterPullback` moves the
	// near plane toward the sun so casters outside the slice still land in the map.
	inline glm::mat4 CascadeViewProj(const Sphere& s, const glm::vec3& lightDir, uint32_t tileResolution,
	                                 float casterPullback)
	{
		const glm::vec3 dir = glm::normalize(lightDir);
		const glm::vec3 up = (glm::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

		const glm::mat4 rot = glm::lookAtRH(glm::vec3(0.0f), dir, up);
		const float texel = 2.0f * s.Radius / static_cast<float>(tileResolution);
		glm::vec3 centerLS = glm::vec3(rot * glm::vec4(s.Center, 1.0f));
		centerLS.x = std::floor(centerLS.x / texel + 0.5f) * texel;
		centerLS.y = std::floor(centerLS.y / texel + 0.5f) * texel;
		const glm::vec3 snapped = glm::vec3(glm::inverse(rot) * glm::vec4(centerLS, 1.0f));

		const float back = s.Radius + casterPullback;
		const glm::mat4 view = glm::lookAtRH(snapped - dir * back, snapped, up);
		const glm::mat4 proj =
		    glm::orthoRH_ZO(-s.Radius, s.Radius, -s.Radius, s.Radius, 0.0f, back + s.Radius);
		return proj * view;
	}

	// World-space size of one shadow texel for a sphere-fit map of `radius`.
	inline float TexelWorldSize(float radius, uint32_t tileResolution)
	{
		return 2.0f * radius / static_cast<float>(tileResolution);
	}

	// Cascades share one square atlas of kCascadeAtlasCols^2 tiles, tile i holding cascade i (row-major).
	constexpr uint32_t kCascadeAtlasCols = 2;

	struct TileRect
	{
		uint32_t X = 0, Y = 0, Size = 0; // pixels
	};

	inline TileRect AtlasTile(uint32_t cascade, uint32_t tileResolution)
	{
		return {(cascade % kCascadeAtlasCols) * tileResolution, (cascade / kCascadeAtlasCols) * tileResolution,
		        tileResolution};
	}

	// UV rect of tile `cascade`: xy offset, zw scale, the layout SampleShadowFactor takes for the spot atlas.
	inline glm::vec4 AtlasUvRect(uint32_t cascade)
	{
		constexpr float s = 1.0f / static_cast<float>(kCascadeAtlasCols);
		return {static_cast<float>(cascade % kCascadeAtlasCols) * s, static_cast<float>(cascade / kCascadeAtlasCols) * s,
		        s, s};
	}

	// Bias multiplier of cascade i against cascade 0: a constant NDC bias is a different world bias per cascade,
	// so it scales with the texel size ratio (equal tile resolutions, hence the radius ratio).
	inline float BiasScale(float radius, float radius0)
	{
		return radius0 > 0.0f ? radius / radius0 : 1.0f;
	}

	struct CascadeFit
	{
		uint32_t Count = 0;
		std::array<float, kMaxCascades> Splits{};
		std::array<float, kMaxCascades> Radius{};
		std::array<glm::mat4, kMaxCascades> ViewProj{};
	};

	// Whole per-frame fit: splits, slice corners, sphere and snapped matrix for each cascade.
	inline CascadeFit ComputeFit(const glm::mat4& invViewProj, float camNear, float camFar, const glm::vec3& lightDir,
	                             uint32_t count, float lambda, uint32_t tileResolution, float casterPullback)
	{
		CascadeFit fit;
		fit.Count = glm::clamp(count, 1u, kMaxCascades);
		fit.Splits = ComputeSplits(camNear, camFar, fit.Count, lambda);
		float sliceNear = camNear;
		for (uint32_t i = 0; i < fit.Count; ++i)
		{
			const Sphere s = FitSphere(SliceCorners(invViewProj, camNear, camFar, sliceNear, fit.Splits[i]));
			fit.Radius[i] = s.Radius;
			fit.ViewProj[i] = CascadeViewProj(s, lightDir, tileResolution, casterPullback);
			sliceNear = fit.Splits[i];
		}
		return fit;
	}
}
