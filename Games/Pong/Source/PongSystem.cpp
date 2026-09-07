#include "PongSystem.hpp"

#include "PongComponents.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <algorithm>

namespace Snowstorm
{
	namespace
	{
		// The playfield, in world units. It is what the camera in Pong.world is framed to see, so moving
		// one without the other puts the walls off screen. Constants rather than component fields on
		// purpose: an example should not make the reader chase a value through the inspector to
		// understand the geometry.
		constexpr float kHalfWidth = 8.0f;  // ball scores past +/- this
		constexpr float kHalfHeight = 4.5f; // ball bounces off +/- this
		constexpr float kPaddleX = 7.0f;
	}

	void PongSystem::Execute(const Timestep ts)
	{
		const float dt = ts.GetSeconds();
		if (dt <= 0.0f)
		{
			return;
		}

		auto& reg = m_World->GetRegistry();
		const auto& input = SingletonView<InputStateSingleton>();

		// Never eat keystrokes while the editor has a text field focused. Costs nothing outside the
		// editor, where this is permanently false.
		const bool keysAvailable = !input.WantTextInput;

		// The ball is read first: the paddles chase it when nobody is driving them, which is what makes
		// the example self-playing and therefore verifiable in a headless run with no keyboard.
		float ballY = 0.0f;
		for (const auto view = View<PongBallComponent, TransformComponent>(); const entt::entity e : view)
		{
			ballY = reg.Read<TransformComponent>(e).Position.y;
			break;
		}

		for (const auto view = View<PongPaddleComponent, TransformComponent>(); const entt::entity e : view)
		{
			const auto& paddle = reg.Read<PongPaddleComponent>(e);

			const bool up = keysAvailable && input.Down.test(paddle.Side == 0 ? Key::W : Key::Up);
			const bool down = keysAvailable && input.Down.test(paddle.Side == 0 ? Key::S : Key::Down);

			float dir = static_cast<float>(up) - static_cast<float>(down);
			if (dir == 0.0f)
			{
				// Unattended: track the ball, so the demo plays itself.
				const float delta = ballY - reg.Read<TransformComponent>(e).Position.y;
				dir = std::clamp(delta * 4.0f, -1.0f, 1.0f);
			}

			const float limit = kHalfHeight - paddle.HalfHeight;
			reg.patch<TransformComponent>(e,
			                              [&](TransformComponent& tr)
			                              {
				                              tr.Position.y = std::clamp(tr.Position.y + dir * paddle.Speed * dt,
				                                                         -limit, limit);
				                              tr.Position.x = paddle.Side == 0 ? -kPaddleX : kPaddleX;
			                              });
		}

		for (const auto view = View<PongBallComponent, TransformComponent>(); const entt::entity e : view)
		{
			glm::vec3 pos = reg.Read<TransformComponent>(e).Position;
			auto ball = reg.Read<PongBallComponent>(e);

			pos += ball.Velocity * dt;

			// Top and bottom walls.
			if ((pos.y > kHalfHeight - ball.Radius && ball.Velocity.y > 0.0f) ||
			    (pos.y < -(kHalfHeight - ball.Radius) && ball.Velocity.y < 0.0f))
			{
				ball.Velocity.y = -ball.Velocity.y;
			}

			// Paddles. Checked against the paddle's current Y, and only while the ball is travelling
			// toward that side, so a ball that clips the edge cannot get trapped inside the bat.
			for (const auto paddles = View<PongPaddleComponent, TransformComponent>();
			     const entt::entity p : paddles)
			{
				const auto& paddle = reg.Read<PongPaddleComponent>(p);
				const float px = paddle.Side == 0 ? -kPaddleX : kPaddleX;
				const bool approaching = paddle.Side == 0 ? ball.Velocity.x < 0.0f : ball.Velocity.x > 0.0f;
				if (!approaching)
				{
					continue;
				}

				const float py = reg.Read<TransformComponent>(p).Position.y;
				if (std::abs(pos.x - px) < ball.Radius + 0.3f && std::abs(pos.y - py) < paddle.HalfHeight)
				{
					ball.Velocity.x = -ball.Velocity.x;
					// Where it hit the bat steers the bounce, which is the one rule that makes Pong a
					// game rather than a screensaver.
					ball.Velocity.y += (pos.y - py) * 3.0f;
					pos.x = px + (paddle.Side == 0 ? 1.0f : -1.0f) * (ball.Radius + 0.3f);
				}
			}

			// Scored: reset to the middle, served toward whoever just conceded.
			if (std::abs(pos.x) > kHalfWidth)
			{
				// Read the side BEFORE resetting the position, or the serve direction is decided from
				// the origin and always goes the same way.
				const bool leftScored = pos.x > 0.0f;
				(leftScored ? ball.ScoreLeft : ball.ScoreRight) += 1;
				SS_INFO("Pong: {} scores, {} - {}", leftScored ? "left" : "right", ball.ScoreLeft,
				        ball.ScoreRight);

				pos = glm::vec3{0.0f};
				ball.Velocity = glm::vec3{leftScored ? 5.0f : -5.0f, 3.0f, 0.0f};
			}

			reg.patch<PongBallComponent>(e, [&](PongBallComponent& b)
			                             { b = ball; });
			reg.patch<TransformComponent>(e, [&](TransformComponent& tr)
			                              { tr.Position = pos; });

			// Proof of life for a headless run, throttled so a 300-frame smoke does not spam.
			if (++m_Ticks % 120 == 0)
			{
				SS_INFO("Pong: ball ({:.2f}, {:.2f}) score {} - {}", pos.x, pos.y, ball.ScoreLeft,
				        ball.ScoreRight);
			}
		}
	}
}
