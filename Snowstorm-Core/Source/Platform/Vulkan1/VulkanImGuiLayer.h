#pragma once

#include "Snowstorm/ImGui/ImGuiLayer.h"
#include "Snowstorm/Renderer/RenderCommandBuffer.h"

namespace Snowstorm {

	class VulkanImGuiLayer : public ImGuiLayer
	{
	public:
		VulkanImGuiLayer();
		VulkanImGuiLayer(const std::string& name);
		virtual ~VulkanImGuiLayer();

		virtual void Begin() override;
		virtual void End() override;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
	private:
		Ref<RenderCommandBuffer> m_RenderCommandBuffer;
		float m_Time = 0.0f;
	};

}
