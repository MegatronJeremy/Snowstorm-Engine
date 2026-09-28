#include "Include/Engine.hlsli"
#include "Include/Ui.hlsli"

// Premultiplied texel (or gradient) times premultiplied vertex colour, blended One / OneMinusSrcAlpha. Layers
// are UNORM and so is the swapchain, so colours stay in the sRGB space CSS authors them in and blend there, as
// a browser does.

float StopPosition(UiGradient g, uint i)
{
	return g.Positions[i >> 2][i & 3];
}

// Where along the gradient this point falls: 0 at the first colour stop's reference, 1 at the end.
float GradientT(UiGradient g, float2 p)
{
	if (g.Kind == UI_GRADIENT_LINEAR)
	{
		const float2 v = g.P1 - g.P0;
		return dot(p - g.P0, v) / max(dot(v, v), 1e-6);
	}
	if (g.Kind == UI_GRADIENT_RADIAL)
	{
		return length((p - g.P0) / max(g.P1, float2(1e-6, 1e-6)));
	}
	// Conic: 0 at the start angle, clockwise, one turn per 1.
	const float2 d = p - g.P0;
	const float turn = atan2(d.x, -d.y) - g.Angle;
	return frac(turn / 6.28318530718);
}

float4 GradientColour(UiGradient g, float2 p)
{
	const uint n = max(g.StopCount, 1u);
	float t = GradientT(g, p);
	const float first = StopPosition(g, 0);
	const float last = StopPosition(g, n - 1);
	if (g.Repeating != 0 && last > first)
	{
		t = first + fmod(fmod(t - first, last - first) + (last - first), last - first);
	}
	if (t <= first)
	{
		return g.Colors[0];
	}
	[loop] for (uint i = 1; i < n; ++i)
	{
		const float b = StopPosition(g, i);
		if (t <= b)
		{
			const float a = StopPosition(g, i - 1);
			const float f = b > a ? (t - a) / (b - a) : 1.0;
			return lerp(g.Colors[i - 1], g.Colors[i], f);
		}
	}
	return g.Colors[n - 1];
}

// Pixel art at any scale ("sharp bilinear"): inside a texel the sample stays on the texel's centre, and only
// the last screen pixel before a texel edge blends into the next one. Nearest sampling would do at whole-number
// scales, but the UI scales by the window height, so texels land 3 pixels wide here and 4 there; this keeps
// them even with a one-pixel antialiased seam instead.
float2 PixelArtUV(Texture2D tex, float2 uv)
{
	float w, h;
	tex.GetDimensions(w, h);
	const float2 size = float2(w, h);
	const float2 texel = uv * size;
	const float2 pixelsPerTexel = 1.0 / max(fwidth(texel), float2(1e-5, 1e-5));
	const float2 fromCentre = frac(texel) - 0.5;
	const float2 plateau = max(0.5 - 0.5 / pixelsPerTexel, 0.0);
	const float2 f = (fromCentre - clamp(fromCentre, -plateau, plateau)) * pixelsPerTexel + 0.5;
	return (floor(texel) + f) / size;
}

float4 main(UiVSOut input) : SV_Target
{
	if (gUi.Flags & UI_FLAG_MASK_WRITE)
	{
		return gUi.MaskValue.xxxx;
	}
	float4 texel = float4(1.0, 1.0, 1.0, 1.0);
	if (gUi.Flags & UI_FLAG_TEXTURED)
	{
		Texture2D tex = Textures[NonUniformResourceIndex(gUi.TextureIndex)];
		const float2 uv = input.UV * gUi.UvScale;
		// The sampler clamps (a 9-slice must not bleed its opposite edge in), so a repeated image wraps here, with
		// the unwrapped coordinates' gradients so the seam picks no mip of its own. Every branch below is uniform
		// per draw (flags), so the derivatives hold.
		const bool repeat = (gUi.Flags & UI_FLAG_REPEAT) != 0;
		if (gUi.Flags & UI_FLAG_PIXEL_ART)
		{
			// Level 0 always: the bent coordinates' derivatives jump at every texel seam and would pick a mip there.
			const float2 bent = PixelArtUV(tex, uv);
			texel = tex.SampleLevel(UiSampler, repeat ? frac(bent) : bent, 0.0);
		}
		else if (repeat)
		{
			texel = tex.SampleGrad(UiSampler, frac(uv), ddx(uv), ddy(uv));
		}
		else
		{
			texel = tex.Sample(UiSampler, uv);
		}
		if (any(gUi.UvScale > 1.0) && any(uv > 1.0))
		{
			texel = float4(0.0, 0.0, 0.0, 0.0); // past the end of a bake clipped to the window
		}
	}
	if (gUi.Flags & UI_FLAG_GRADIENT)
	{
		texel *= GradientColour(UiGradients[gUi.GradientIndex], input.Local);
	}
	float4 colour = texel * input.Colour;
	// A clip mask is a window-sized coverage image, read at this pixel: no stencil, and its edges keep the
	// antialiasing the geometry that drew it had.
	if (gUi.MaskIndex != 0)
	{
		colour *= Textures[NonUniformResourceIndex(gUi.MaskIndex)].Load(int3(input.PositionCS.xy, 0)).r;
	}
	return colour;
}
