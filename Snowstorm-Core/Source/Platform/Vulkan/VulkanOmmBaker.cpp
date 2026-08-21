#include "VulkanOmmBaker.hpp"

#include "VulkanComputePipeline.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	VulkanOmmBaker& VulkanOmmBaker::Get()
	{
		static VulkanOmmBaker instance;
		return instance;
	}

	void VulkanOmmBaker::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		const Ref<Shader> cs =
		    Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/OmmBake.comp.hlsl");
		if (!cs || !cs->IsReady())
		{
			return; // async compile; retried on the next bake
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "OmmBakePipeline";
		// Whole BakeConstants block as a compute push constant (Pass flips between the two dispatches).
		PushConstantRangeDesc pc{};
		pc.Offset = 0;
		pc.Size = sizeof(OmmBakeConstants);
		pc.Stages = ShaderStage::Compute;
		p.PushConstants = {pc};
		m_Pipeline = Pipeline::Create(p);

		// Wrapping linear sampler for the albedo alpha lookup (foliage atlases tile). Alpha is a coverage mask,
		// so linear vs point barely matters; wrap avoids edge artifacts on tiled UVs.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::Repeat;
		s.AddressV = SamplerAddressMode::Repeat;
		s.AddressW = SamplerAddressMode::Repeat;
		s.EnableAnisotropy = false;
		s.DebugName = "OmmBakeSampler";
		m_Sampler = Sampler::Create(s);
	}

	bool VulkanOmmBaker::IsReady()
	{
		EnsureResources();
		return m_Pipeline != nullptr;
	}

	void VulkanOmmBaker::Shutdown()
	{
		m_Pipeline.reset();
		m_Sampler.reset();
	}

	VkPipeline VulkanOmmBaker::GetPipelineHandle() const
	{
		return std::static_pointer_cast<VulkanComputePipeline>(m_Pipeline)->GetHandle();
	}

	VkPipelineLayout VulkanOmmBaker::GetPipelineLayout() const
	{
		return std::static_pointer_cast<VulkanComputePipeline>(m_Pipeline)->GetPipelineLayout();
	}
}
