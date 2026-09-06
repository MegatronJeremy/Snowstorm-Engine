#include "SpriteTestModule.hpp"

#include "Snowstorm/Components/SpriteComponent.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

#include <filesystem>
#include <utility>

namespace Snowstorm
{
	SpriteTestModule::SpriteTestModule(std::string imagePath)
	    : m_ImagePath(std::move(imagePath))
	{
	}

	void SpriteTestModule::OnAttach(World& world)
	{
		const Ref<Texture> texture = Texture::Create(std::filesystem::path(m_ImagePath), true);
		if (!texture)
		{
			SS_CORE_ERROR("SpriteTest: could not load '{}'; no test sprites spawned.", m_ImagePath);
			return;
		}
		const Ref<TextureView> view = texture->GetDefaultView();
		const glm::vec2 size{static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight())};

		auto base = world.CreateEntity("Sprite test (opaque)");
		auto& baseSprite = base.AddComponent<SpriteComponent>();
		baseSprite.TextureInstance = view;
		baseSprite.Position = {64.0f, 64.0f};
		baseSprite.Size = size;
		baseSprite.Layer = 0;

		auto over = world.CreateEntity("Sprite test (tinted overlay)");
		auto& overSprite = over.AddComponent<SpriteComponent>();
		overSprite.TextureInstance = view;
		overSprite.Position = {64.0f + size.x * 0.5f, 64.0f + size.y * 0.5f};
		overSprite.Size = size;
		overSprite.TintColor = {1.0f, 0.4f, 0.4f, 0.5f};
		overSprite.Layer = 1;

		SS_CORE_INFO("SpriteTest: spawned two {}x{} sprites from '{}'.", texture->GetWidth(), texture->GetHeight(), m_ImagePath);
	}
}
