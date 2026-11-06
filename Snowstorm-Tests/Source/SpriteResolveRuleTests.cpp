#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/SpriteResolveRule.hpp"

using namespace Snowstorm;

namespace
{
	class StubView final : public TextureView
	{
	public:
		const TextureViewDesc& GetDesc() const override { return m_Desc; }
		const Ref<Texture>& GetTexture() const override { return m_Texture; }
		uint64_t GetUIID() const override { return 0; }
		bool operator==(const TextureView& other) const override { return this == &other; }

	private:
		TextureViewDesc m_Desc{};
		Ref<Texture> m_Texture;
	};

	SpriteComponent Sprite(const uint64_t texture, const uint64_t resolvedFrom, const bool view)
	{
		SpriteComponent s;
		s.Texture = AssetHandle{texture};
		s.ResolvedFrom = AssetHandle{resolvedFrom};
		if (view)
			s.View = CreateRef<StubView>();
		return s;
	}
}

TEST_CASE("Texture 0 leaves a game-bound view alone", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(0, 0, true)) == SpriteResolveAction::None);
	REQUIRE(DecideSpriteResolve(Sprite(0, 0, false)) == SpriteResolveAction::None);
}

TEST_CASE("A texture without a view resolves", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(5, 0, false)) == SpriteResolveAction::Resolve);
}

TEST_CASE("A resolved sprite is stable until Texture changes", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(5, 5, true)) == SpriteResolveAction::None);
	REQUIRE(DecideSpriteResolve(Sprite(6, 5, true)) == SpriteResolveAction::Resolve);
}

TEST_CASE("A recorded source whose view is gone resolves again", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(5, 5, false)) == SpriteResolveAction::Resolve);
}

TEST_CASE("Clearing Texture drops only a system-set view", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(0, 5, true)) == SpriteResolveAction::Drop);
}

TEST_CASE("A game view over a Texture is replaced by the resolve", "[sprite]")
{
	REQUIRE(DecideSpriteResolve(Sprite(5, 0, true)) == SpriteResolveAction::Resolve);
}
