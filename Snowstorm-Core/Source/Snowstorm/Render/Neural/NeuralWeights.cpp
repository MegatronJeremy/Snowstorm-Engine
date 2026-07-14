#include "NeuralWeights.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <cstring>
#include <fstream>

namespace Snowstorm::Neural
{
	namespace
	{
		constexpr char kMagic[4] = {'S', 'S', 'N', 'N'};
		constexpr uint32_t kVersion = 1;

		size_t LayerWeightCount(const ConvLayer& l)
		{
			return static_cast<size_t>(l.OutChannels) * l.InChannels * l.KernelSize * l.KernelSize;
		}

		template <typename T>
		void WritePod(std::ofstream& f, const T& v)
		{
			f.write(reinterpret_cast<const char*>(&v), sizeof(T));
		}

		template <typename T>
		bool ReadPod(std::ifstream& f, T& v)
		{
			f.read(reinterpret_cast<char*>(&v), sizeof(T));
			return static_cast<bool>(f);
		}
	}

	size_t NeuralModel::TotalFloats() const
	{
		size_t n = 0;
		for (const ConvLayer& l : Layers)
		{
			n += LayerWeightCount(l) + l.OutChannels; // weights + bias
		}
		return n;
	}

	std::vector<size_t> NeuralModel::LayerFloatOffsets() const
	{
		std::vector<size_t> offsets;
		offsets.reserve(Layers.size());
		size_t cursor = 0;
		for (const ConvLayer& l : Layers)
		{
			offsets.push_back(cursor);
			cursor += LayerWeightCount(l) + l.OutChannels;
		}
		return offsets;
	}

	std::vector<float> NeuralModel::PackWeights() const
	{
		std::vector<float> packed;
		packed.reserve(TotalFloats());
		for (const ConvLayer& l : Layers)
		{
			packed.insert(packed.end(), l.Weights.begin(), l.Weights.end());
			packed.insert(packed.end(), l.Bias.begin(), l.Bias.end());
		}
		return packed;
	}

	NeuralModel MakeIdentityRefiner(const uint32_t inChannels)
	{
		// inChannels -> 16 -> 16 -> 3, all 3x3. Hidden layers ReLU, output layer linear. The output layer is
		// all-zero so the residual is 0 (output == bilinear input, feature channels 0..2). Hidden-layer weights
		// are irrelevant to the identity property but are set to zero too for a clean, reproducible file.
		//
		// inChannels selects the inference path's feature stack: 3 = spatial (bilinear LR only, #47); 8 = temporal
		// (bilinear LR 0..2, MV-warped previous output 3..5, motion vector 6..7, #98). The count MUST match how
		// many feature channels the pass populates before the conv stack (upsample writes 0..2; the warp stage
		// writes 3..7 only on the temporal path) — a wider identity than the pass fills would read uninitialized
		// feature memory. All first-layer weights are zero regardless, so identity output is exactly bilinear.
		NeuralModel m;
		const auto makeLayer = [](const uint32_t inC, const uint32_t outC, const Activation act)
		{
			ConvLayer l;
			l.InChannels = inC;
			l.OutChannels = outC;
			l.KernelSize = 3;
			l.Act = act;
			l.Weights.assign(static_cast<size_t>(outC) * inC * 3 * 3, 0.0f);
			l.Bias.assign(outC, 0.0f);
			return l;
		};
		m.Layers.push_back(makeLayer(inChannels, 16, Activation::ReLU));
		m.Layers.push_back(makeLayer(16, 16, Activation::ReLU));
		m.Layers.push_back(makeLayer(16, 3, Activation::None)); // zero -> residual 0 -> exact bilinear
		return m;
	}

	bool SaveModel(const std::string& path, const NeuralModel& model)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f)
		{
			SS_CORE_ERROR("SaveModel: cannot open '{}' for writing", path);
			return false;
		}

		f.write(kMagic, 4);
		WritePod(f, kVersion);
		WritePod(f, static_cast<uint32_t>(model.Layers.size()));
		for (const ConvLayer& l : model.Layers)
		{
			WritePod(f, l.InChannels);
			WritePod(f, l.OutChannels);
			WritePod(f, l.KernelSize);
			WritePod(f, static_cast<uint32_t>(l.Act));
			f.write(reinterpret_cast<const char*>(l.Weights.data()),
			        static_cast<std::streamsize>(l.Weights.size() * sizeof(float)));
			f.write(reinterpret_cast<const char*>(l.Bias.data()),
			        static_cast<std::streamsize>(l.Bias.size() * sizeof(float)));
		}
		return static_cast<bool>(f);
	}

	bool LoadModel(const std::string& path, NeuralModel& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
		{
			SS_CORE_ERROR("LoadModel: cannot open '{}'", path);
			return false;
		}

		char magic[4];
		f.read(magic, 4);
		if (!f || std::memcmp(magic, kMagic, 4) != 0)
		{
			SS_CORE_ERROR("LoadModel: '{}' is not a valid .ssnn (bad magic)", path);
			return false;
		}
		uint32_t version = 0, layerCount = 0;
		if (!ReadPod(f, version) || version != kVersion)
		{
			SS_CORE_ERROR("LoadModel: '{}' unsupported version {}", path, version);
			return false;
		}
		if (!ReadPod(f, layerCount) || layerCount == 0 || layerCount > 256)
		{
			SS_CORE_ERROR("LoadModel: '{}' bad layer count {}", path, layerCount);
			return false;
		}

		out.Layers.clear();
		out.Layers.reserve(layerCount);
		for (uint32_t i = 0; i < layerCount; ++i)
		{
			ConvLayer l;
			uint32_t act = 0;
			if (!ReadPod(f, l.InChannels) || !ReadPod(f, l.OutChannels) || !ReadPod(f, l.KernelSize) || !ReadPod(f, act))
			{
				SS_CORE_ERROR("LoadModel: '{}' truncated at layer {} header", path, i);
				return false;
			}
			l.Act = static_cast<Activation>(act);
			if (l.InChannels == 0 || l.OutChannels == 0 || (l.KernelSize != 1 && l.KernelSize != 3))
			{
				SS_CORE_ERROR("LoadModel: '{}' invalid layer {} dims", path, i);
				return false;
			}
			l.Weights.resize(LayerWeightCount(l));
			l.Bias.resize(l.OutChannels);
			f.read(reinterpret_cast<char*>(l.Weights.data()), static_cast<std::streamsize>(l.Weights.size() * sizeof(float)));
			f.read(reinterpret_cast<char*>(l.Bias.data()), static_cast<std::streamsize>(l.Bias.size() * sizeof(float)));
			if (!f)
			{
				SS_CORE_ERROR("LoadModel: '{}' truncated at layer {} data", path, i);
				return false;
			}
			out.Layers.push_back(std::move(l));
		}
		return true;
	}
}
