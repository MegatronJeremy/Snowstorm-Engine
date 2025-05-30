#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Framebuffer.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	struct FramebufferComponent
	{
		Ref<Framebuffer> Framebuffer;
		bool Active = true;
	};
}

