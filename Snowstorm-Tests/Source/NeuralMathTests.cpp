#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Render/Neural/NeuralMath.hpp"

using namespace Snowstorm::Neural;
using Catch::Approx;

namespace
{
	// Build a 1-channel HxW feature map from a row-major initializer.
	FeatureMap Make1(const uint32_t h, const uint32_t w, std::vector<float> data)
	{
		FeatureMap f;
		f.Channels = 1;
		f.Height = h;
		f.Width = w;
		f.Data = std::move(data);
		return f;
	}
}

// A 1x1 kernel scales each pixel by the single weight and adds bias — the simplest conv, no neighbourhood.
// Verifies the core accumulate + bias + no-padding path independent of kernel geometry.
TEST_CASE("Conv2D 1x1 kernel is a per-pixel affine map", "[neural][conv]")
{
	const FeatureMap in = Make1(2, 2, {1.0f, 2.0f, 3.0f, 4.0f});
	ConvLayer layer;
	layer.InChannels = 1;
	layer.OutChannels = 1;
	layer.KernelSize = 1;
	layer.Act = Activation::None;
	layer.Weights = {2.0f}; // out = 2*in + bias
	layer.Bias = {0.5f};

	const FeatureMap out = Conv2DReference(in, layer);
	REQUIRE(out.Channels == 1);
	REQUIRE(out.Height == 2);
	REQUIRE(out.Width == 2);
	CHECK(out.At(0, 0, 0) == Approx(2.5f)); // 2*1 + 0.5
	CHECK(out.At(0, 0, 1) == Approx(4.5f)); // 2*2 + 0.5
	CHECK(out.At(0, 1, 0) == Approx(6.5f)); // 2*3 + 0.5
	CHECK(out.At(0, 1, 1) == Approx(8.5f)); // 2*4 + 0.5
}

// 3x3 conv with a known kernel on a 3x3 image, hand-computed at the CENTER (full neighbourhood) and a
// CORNER (zero-padded neighbours) to pin the same-padding border behaviour.
TEST_CASE("Conv2D 3x3 same-padding matches hand computation", "[neural][conv]")
{
	// image:  1 2 3
	//         4 5 6
	//         7 8 9
	const FeatureMap in = Make1(3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9});

	ConvLayer layer;
	layer.InChannels = 1;
	layer.OutChannels = 1;
	layer.KernelSize = 3;
	layer.Act = Activation::None;
	// Identity-ish kernel: center tap = 1, all others 0 -> output == input. Guards indexing/orientation.
	layer.Weights = {0, 0, 0,
	                 0, 1, 0,
	                 0, 0, 0};
	layer.Bias = {0.0f};

	const FeatureMap id = Conv2DReference(in, layer);
	for (uint32_t y = 0; y < 3; ++y)
	{
		for (uint32_t x = 0; x < 3; ++x)
		{
			CHECK(id.At(0, y, x) == Approx(in.At(0, y, x)));
		}
	}

	// A box-sum kernel (all ones): each output = sum of the 3x3 neighbourhood, zero-padded at borders.
	layer.Weights.assign(9, 1.0f);
	const FeatureMap box = Conv2DReference(in, layer);
	// Center (1,1): sum of all nine = 45.
	CHECK(box.At(0, 1, 1) == Approx(45.0f));
	// Corner (0,0): neighbours above/left are zero-padded -> sum of {0,0,0, 0,1,2, 0,4,5} = 12.
	CHECK(box.At(0, 0, 0) == Approx(12.0f));
	// Corner (2,2): sum of {5,6,0, 8,9,0, 0,0,0} = 28.
	CHECK(box.At(0, 2, 2) == Approx(28.0f));
}

// ReLU clamps negative accumulations to zero; None passes them through.
TEST_CASE("Conv2D activation ReLU clamps negatives", "[neural][conv]")
{
	const FeatureMap in = Make1(1, 1, {1.0f});
	ConvLayer layer;
	layer.InChannels = 1;
	layer.OutChannels = 1;
	layer.KernelSize = 1;
	layer.Weights = {-3.0f};
	layer.Bias = {0.0f};

	layer.Act = Activation::None;
	CHECK(Conv2DReference(in, layer).At(0, 0, 0) == Approx(-3.0f));

	layer.Act = Activation::ReLU;
	CHECK(Conv2DReference(in, layer).At(0, 0, 0) == Approx(0.0f));
}

// Multi-channel: 2 input channels summed into 1 output channel verifies the inner ic accumulation.
TEST_CASE("Conv2D sums across input channels", "[neural][conv]")
{
	FeatureMap in;
	in.Channels = 2;
	in.Height = 1;
	in.Width = 1;
	in.Data = {3.0f /*ch0*/, 5.0f /*ch1*/};

	ConvLayer layer;
	layer.InChannels = 2;
	layer.OutChannels = 1;
	layer.KernelSize = 1;
	layer.Act = Activation::None;
	layer.Weights = {2.0f /*oc0,ic0*/, 10.0f /*oc0,ic1*/};
	layer.Bias = {1.0f};

	// 2*3 + 10*5 + 1 = 57
	CHECK(Conv2DReference(in, layer).At(0, 0, 0) == Approx(57.0f));
}
