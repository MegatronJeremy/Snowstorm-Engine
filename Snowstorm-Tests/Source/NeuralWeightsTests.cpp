#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Render/Neural/NeuralWeights.hpp"

#include <cstdio>
#include <fstream>

using namespace Snowstorm::Neural;
using Catch::Approx;

// The identity refiner's final layer must be all-zero so its residual is exactly 0 — that's what makes the
// untrained network a provable bilinear no-op (the correctness oracle for the whole GPU path).
TEST_CASE("Identity refiner has a zero output layer", "[neural][weights]")
{
	const NeuralModel m = MakeIdentityRefiner();
	REQUIRE(m.Layers.size() == 3);
	CHECK(m.Layers.front().InChannels == 3);
	CHECK(m.Layers.back().OutChannels == 3);

	const ConvLayer& last = m.Layers.back();
	CHECK(last.Act == Activation::None);
	for (const float w : last.Weights)
	{
		CHECK(w == Approx(0.0f));
	}
	for (const float b : last.Bias)
	{
		CHECK(b == Approx(0.0f));
	}
}

// PackWeights + LayerFloatOffsets must describe one contiguous buffer the GPU uploads, with each layer's
// bias immediately after its weights and offsets landing on layer boundaries.
TEST_CASE("Model packs weights contiguously with correct offsets", "[neural][weights]")
{
	const NeuralModel m = MakeIdentityRefiner();
	const std::vector<float> packed = m.PackWeights();
	const std::vector<size_t> offsets = m.LayerFloatOffsets();

	REQUIRE(packed.size() == m.TotalFloats());
	REQUIRE(offsets.size() == m.Layers.size());
	CHECK(offsets[0] == 0);

	// Each layer occupies weights + bias floats; the next offset is the running sum.
	size_t cursor = 0;
	for (size_t i = 0; i < m.Layers.size(); ++i)
	{
		CHECK(offsets[i] == cursor);
		const ConvLayer& l = m.Layers[i];
		cursor += l.Weights.size() + l.Bias.size();
	}
	CHECK(cursor == packed.size());
}

// .ssnn save -> load must reconstruct the model exactly (the training pipeline writes it, the engine reads it).
TEST_CASE("SSNN save/load round-trips a model", "[neural][weights]")
{
	NeuralModel m;
	ConvLayer a;
	a.InChannels = 2;
	a.OutChannels = 3;
	a.KernelSize = 3;
	a.Act = Activation::ReLU;
	a.Weights.resize(3 * 2 * 3 * 3);
	for (size_t i = 0; i < a.Weights.size(); ++i)
	{
		a.Weights[i] = static_cast<float>(i) * 0.25f - 1.0f;
	}
	a.Bias = {0.1f, -0.2f, 0.3f};
	m.Layers.push_back(a);

	ConvLayer b;
	b.InChannels = 3;
	b.OutChannels = 1;
	b.KernelSize = 1;
	b.Act = Activation::None;
	b.Weights = {1.5f, -2.5f, 3.5f};
	b.Bias = {0.75f};
	m.Layers.push_back(b);

	const std::string path = "ssnn_roundtrip_test.ssnn";
	REQUIRE(SaveModel(path, m));

	NeuralModel loaded;
	REQUIRE(LoadModel(path, loaded));
	REQUIRE(loaded.Layers.size() == 2);

	for (size_t li = 0; li < m.Layers.size(); ++li)
	{
		const ConvLayer& e = m.Layers[li];
		const ConvLayer& g = loaded.Layers[li];
		CHECK(g.InChannels == e.InChannels);
		CHECK(g.OutChannels == e.OutChannels);
		CHECK(g.KernelSize == e.KernelSize);
		CHECK(g.Act == e.Act);
		REQUIRE(g.Weights.size() == e.Weights.size());
		for (size_t i = 0; i < e.Weights.size(); ++i)
		{
			CHECK(g.Weights[i] == Approx(e.Weights[i]));
		}
		REQUIRE(g.Bias.size() == e.Bias.size());
		for (size_t i = 0; i < e.Bias.size(); ++i)
		{
			CHECK(g.Bias[i] == Approx(e.Bias[i]));
		}
	}

	std::remove(path.c_str());
}

// A bad-magic file must be rejected, not silently accepted (fail loud).
TEST_CASE("LoadModel rejects a non-ssnn file", "[neural][weights]")
{
	const std::string path = "not_an_ssnn_test.bin";
	{
		std::ofstream f(path, std::ios::binary);
		const char junk[16] = {'N', 'O', 'P', 'E'};
		f.write(junk, sizeof(junk));
	}
	NeuralModel out;
	CHECK_FALSE(LoadModel(path, out));
	std::remove(path.c_str());
}
