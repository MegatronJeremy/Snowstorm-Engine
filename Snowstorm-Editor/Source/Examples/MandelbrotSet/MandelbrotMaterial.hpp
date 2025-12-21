#pragma once

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

namespace Snowstorm
{
	class MandelbrotMaterial final : public NonCopyable
	{
	public:
		struct Params
		{
			glm::vec2 Center{ -0.75f, 0.0f };
			float Zoom = 4.0f;
			int MaxIterations = 100;
		};

		explicit MandelbrotMaterial(const Ref<MaterialInstance>& materialInstance)
			: m_Instance(materialInstance)
		{
			SS_ASSERT(m_Instance, "MandelbrotMaterial requires a MaterialInstance");
			ApplyToInstance();
		}

		void SetCenter(const glm::vec2& center) { m_Params.Center = center; ApplyToInstance(); }
		void SetZoom(float zoom) { m_Params.Zoom = zoom; ApplyToInstance(); }
		void SetMaxIterations(int iterations) { m_Params.MaxIterations = iterations; ApplyToInstance(); }

		[[nodiscard]] const Ref<MaterialInstance>& GetMaterialInstance() const { return m_Instance; }

		[[nodiscard]] const glm::vec2& GetCenter() const { return m_Params.Center; }
		[[nodiscard]] float GetZoom() const { return m_Params.Zoom; }
		[[nodiscard]] int GetMaxIterations() const { return m_Params.MaxIterations; }

	private:
		void ApplyToInstance() const
		{
			// ObjectCB.Extras0 = float4(Center.xy, Zoom, float(MaxIterations))
			m_Instance->SetObjectExtras0(glm::vec4(
				m_Params.Center.x,
				m_Params.Center.y,
				m_Params.Zoom,
				static_cast<float>(m_Params.MaxIterations)));
		}

	private:
		Ref<MaterialInstance> m_Instance;
		Params m_Params{};
	};
}
