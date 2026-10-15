#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"

using namespace Snowstorm;

namespace
{
	class FakeTexture final : public Texture
	{
	public:
		[[nodiscard]] const TextureDesc& GetDesc() const override { return m_Desc; }
		[[nodiscard]] Ref<TextureView> GetDefaultView() override { return nullptr; }
		void SetData(const void*, uint32_t) override {}
		void SetMipData(const std::vector<std::vector<uint8_t>>&) override {}
		void SetCubeData(const std::vector<std::vector<std::vector<uint8_t>>>&) override {}
		bool operator==(const Texture& other) const override { return this == &other; }

	private:
		TextureDesc m_Desc;
	};

	Ref<Texture> Tex()
	{
		return CreateRef<FakeTexture>();
	}

	RenderGraph::Pass Compute(const char* name, std::vector<Ref<Texture>> reads, std::vector<Ref<Texture>> writes,
	                          GpuQueue queue = GpuQueue::AsyncCompute)
	{
		RenderGraph::Pass p;
		p.Name = name;
		p.IsCompute = queue == GpuQueue::AsyncCompute;
		p.Queue = queue;
		for (auto& r : reads)
		{
			p.Reads.push_back({r, RenderGraph::AccessState::Sampled});
		}
		for (auto& w : writes)
		{
			p.Writes.push_back({w, RenderGraph::AccessState::Storage});
		}
		return p;
	}

	struct ReorderOn
	{
		ReorderOn() : m_Prev(CVars::GraphReorder.Get()) { CVars::GraphReorder.Set(true); }
		~ReorderOn() { CVars::GraphReorder.Set(m_Prev); }
		bool m_Prev;
	};
}

TEST_CASE("Dependencies derive RAW, WAR and WAW from declared textures", "[render][graph]")
{
	const auto a = Tex();
	RenderGraph g;
	g.AddPass(Compute("w0", {}, {a}));
	g.AddPass(Compute("r", {a}, {}));
	g.AddPass(Compute("w1", {}, {a}));

	const auto deps = g.BuildDependencies();
	REQUIRE(deps[0].empty());
	REQUIRE(deps[1].size() == 1);
	CHECK(deps[1][0].Producer == 0);
	CHECK(deps[1][0].Kind == RenderGraph::Hazard::ReadAfterWrite);
	REQUIRE(deps[2].size() == 2);
	CHECK(deps[2][0].Producer == 1);
	CHECK(deps[2][0].Kind == RenderGraph::Hazard::WriteAfterRead);
	CHECK(deps[2][1].Producer == 0);
	CHECK(deps[2][1].Kind == RenderGraph::Hazard::WriteAfterWrite);
}

TEST_CASE("Schedule is declaration order while reordering is off", "[render][graph]")
{
	RenderGraph g;
	g.AddPass(Compute("g0", {}, {}, GpuQueue::Graphics));
	g.AddPass(Compute("c0", {}, {Tex()}));
	g.AddPass(Compute("g1", {}, {}, GpuQueue::Graphics));

	const bool prev = CVars::GraphReorder.Get();
	CVars::GraphReorder.Set(false);
	CHECK(g.BuildSchedule(nullptr) == std::vector<uint32_t>{0, 1, 2});
	CVars::GraphReorder.Set(prev);
}

TEST_CASE("Independent compute passes coalesce past an interleaved graphics pass", "[render][graph]")
{
	const ReorderOn on;
	const auto x = Tex();
	const auto y = Tex();
	RenderGraph g;
	g.AddPass(Compute("cA", {}, {x}));
	g.AddPass(Compute("gfx", {}, {}, GpuQueue::Graphics));
	g.AddPass(Compute("cB", {}, {y}));

	// cB has no edge to gfx, so it is pulled next to cA and the two share one fork/join.
	CHECK(g.BuildSchedule(nullptr) == std::vector<uint32_t>{0, 2, 1});
}

TEST_CASE("A compute pass never moves before its producer", "[render][graph]")
{
	const ReorderOn on;
	const auto x = Tex();
	RenderGraph g;
	g.AddPass(Compute("gfxWrite", {}, {x}, GpuQueue::Graphics));
	g.AddPass(Compute("gfx", {}, {}, GpuQueue::Graphics));
	g.AddPass(Compute("cRead", {x}, {}));

	const auto order = g.BuildSchedule(nullptr);
	REQUIRE(order.size() == 3);
	CHECK(order[0] == 0);
	CHECK(order[1] == 2);
	CHECK(order[2] == 1);
}

TEST_CASE("Graphics passes keep their relative order", "[render][graph]")
{
	const ReorderOn on;
	RenderGraph g;
	g.AddPass(Compute("g0", {}, {}, GpuQueue::Graphics));
	g.AddPass(Compute("g1", {}, {}, GpuQueue::Graphics));
	g.AddPass(Compute("g2", {}, {}, GpuQueue::Graphics));

	CHECK(g.BuildSchedule(nullptr) == std::vector<uint32_t>{0, 1, 2});
}
