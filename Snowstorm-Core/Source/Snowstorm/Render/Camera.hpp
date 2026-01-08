#pragma once

#include "Math.hpp"

namespace Snowstorm
{
	class Camera
	{
	public:
		Camera() = default;

		explicit Camera(const glm::mat4& projection)
			: m_Projection(projection)
		{
		}

		virtual ~Camera() = default;

		const glm::mat4& GetProjection() const { return m_Projection; }

	protected:
		glm::mat4 m_Projection{1.0f};
	};
}
