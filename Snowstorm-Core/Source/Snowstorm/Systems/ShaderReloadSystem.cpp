#include "ShaderReloadSystem.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	void ShaderReloadSystem::Execute(const Timestep ts)
	{
		static float timeSinceLastCheck = 0.0f;
		timeSinceLastCheck += ts.GetSeconds();

		//-- check for updates every 1 second
		if (timeSinceLastCheck > 1.0f)
		{
			auto& shaderLibrary = ServiceView<ShaderLibrary>();

			// 1) Recompile any shader whose source (or an included header) changed -> bumps its version and
			//    writes fresh SPIR-V to the cache.
			shaderLibrary.ReloadAll();

			// 2) Rebuild any live pipeline whose shader version advanced. A pipeline bakes its VkShaderModules
			//    at creation, so recompiled SPIR-V is invisible until the VkPipeline is rebuilt from it. Each
			//    Reload() self-skips when its shader is unchanged, so this sweep is cheap when nothing changed.
			//    Runs in the AssetSync phase (before Render), so no command buffer is open when the pipeline
			//    drains the device and swaps its handle.
			Pipeline::ForEachLive([](const Ref<Pipeline>& pipeline)
			                      { pipeline->Reload(); });

			timeSinceLastCheck = 0.0f;
		}
	}
}
