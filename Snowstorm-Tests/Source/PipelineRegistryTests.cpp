#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/Pipeline.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace Snowstorm;

namespace
{
	class FakePipeline final : public Pipeline
	{
	public:
		static Ref<FakePipeline> Make()
		{
			auto p = CreateRef<FakePipeline>();
			Register(p);
			return p;
		}

		[[nodiscard]] const PipelineDesc& GetDesc() const override { return m_Desc; }
		[[nodiscard]] const std::vector<Ref<DescriptorSetLayout>>& GetSetLayouts() const override { return m_Layouts; }

	private:
		PipelineDesc m_Desc;
		std::vector<Ref<DescriptorSetLayout>> m_Layouts;
	};

	size_t CountLive()
	{
		size_t n = 0;
		Pipeline::ForEachLive([&](const Ref<Pipeline>&)
		                      { ++n; });
		return n;
	}
}

TEST_CASE("pipeline registry is thread-safe and weak", "[pipeline][threading]")
{
	const size_t baseline = CountLive();

	constexpr int kThreads = 8;
	constexpr int kPerThread = 200;
	std::vector<std::vector<Ref<FakePipeline>>> kept(kThreads);
	std::atomic<bool> go{false};
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; ++t)
	{
		threads.emplace_back([&, t]
		                     {
			while (!go.load()) {}
			for (int i = 0; i < kPerThread; ++i)
			{
				auto p = FakePipeline::Make();
				if (i % 2 == 0) kept[t].push_back(p); // odd ones die immediately
				if (i % 50 == 0) CountLive();
			} });
	}
	go = true;
	for (auto& th : threads)
		th.join();

	REQUIRE(CountLive() == baseline + kThreads * kPerThread / 2);
	kept.clear();
	REQUIRE(CountLive() == baseline);
}

TEST_CASE("ForEachLive callback may create pipelines", "[pipeline][threading]")
{
	auto a = FakePipeline::Make();
	Ref<FakePipeline> made;
	Pipeline::ForEachLive([&](const Ref<Pipeline>& p)
	                      { if (p == a && !made) made = FakePipeline::Make(); });
	REQUIRE(made != nullptr);
}
