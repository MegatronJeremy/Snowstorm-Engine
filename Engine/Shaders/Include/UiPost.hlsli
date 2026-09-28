// UiPost.hlsli - the UI layer passes (Snowstorm/UI): filters, compositing one layer onto another, saving a
// region of a layer as a texture. Fullscreen over the target; everything per pass rides in the push constant,
// including the bindless indices of what it samples.

#ifndef SNOWSTORM_UIPOST_HLSLI
#define SNOWSTORM_UIPOST_HLSLI

SamplerState UiPostSampler : register(s5, space1);

#define UI_POST_COPY 0u          // the source, sampled at SampleOffset + uv * SampleScale
#define UI_POST_OPACITY 1u       // the source times Colour.a
#define UI_POST_COLOR_MATRIX 2u  // straight rgb' = R*r + G*g + B*b + T, alpha kept (brightness, sepia...)
#define UI_POST_BLUR 3u          // one direction of a Gaussian: TexelStep per tap, Sigma in taps
#define UI_POST_SHADOW 4u        // Colour times the source's alpha, moved by ShadowOffset
#define UI_POST_OVER 5u          // Texture2 (the original) over the source (its blurred shadow)
#define UI_POST_MASK_MULTIPLY 6u // clip mask intersection: the source's red times Texture2's red

// Mirrors PostPush in UiRenderer.hpp field-for-field (128 bytes, the push-constant minimum).
struct UiPostPush
{
	float4 MatrixR; // colour matrix: xyz is what r contributes to r'g'b', w is the translation of r'
	float4 MatrixG;
	float4 MatrixB;
	float4 Colour;       // shadow colour (premultiplied), or opacity in .a
	float2 SampleOffset; // source UV = SampleOffset + uv * SampleScale
	float2 SampleScale;
	float2 ShadowOffset; // in source UV
	float2 TexelStep;    // blur direction, in source UV per tap
	float Sigma;         // blur, in taps
	uint Mode;
	uint Texture;
	uint Texture2;
	uint Mask;           // bindless index of a clip mask applied to the output, 0 for none
	uint3 _Pad;
};

[[vk::push_constant]] UiPostPush gPost;

struct UiPostVSOut
{
	float4 PositionCS : SV_Position;
	float2 UV : TEXCOORD0;
};

#endif // SNOWSTORM_UIPOST_HLSLI
