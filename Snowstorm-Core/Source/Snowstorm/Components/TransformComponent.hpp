#pragma once

#include <glm/ext/matrix_transform.hpp>

namespace Snowstorm
{
	struct TransformComponent
	{
		glm::vec3 Position{0.0f, 0.0f, 0.0f};
		glm::vec3 Rotation{0.0f, 0.0f, 0.0f}; //-- stored in radians
		glm::vec3 Scale{1.0f, 1.0f, 1.0f};

		[[nodiscard]] glm::mat4 GetTransformMatrix() const
		{
			glm::mat4 transform = translate(glm::mat4(1.0f), Position);

			//-- order: Y (yaw) → X (pitch) → Z (roll)
			transform = rotate(transform, Rotation.y, glm::vec3(0, 1, 0));
			transform = rotate(transform, Rotation.x, glm::vec3(1, 0, 0));
			transform = rotate(transform, Rotation.z, glm::vec3(0, 0, 1));

			transform = scale(transform, Scale);
			return transform;
		}

		operator glm::mat4() const { return GetTransformMatrix(); }
	};
}