#include "ShaderReloadSystem.hpp"

#include "Snowstorm/Assets/MaterialAsset.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	void ShaderReloadSystem::Execute(const Timestep ts)
	{
		auto& shaderLibrary = ServiceView<ShaderLibrary>();

		bool needPipelineRebuild = false;

		// DefaultLit RT-permutation swap (#118 perf): the lit shader compiles the cheap non-RT variant when no
		// RT effect is active, and the heavy RT variant otherwise. A permutation change is NOT an mtime change,
		// so ReloadAll below won't catch it — drive it explicitly here. Checked every frame (a cheap CVar read)
		// so toggling an RT effect swaps promptly. The key is the composite the mesh pipeline loads.
		{
			const std::string litKey = std::string("Engine/Shaders/Mesh.vert.hlsl|") + kDefaultFragmentShader;
			if (shaderLibrary.Exists(litKey))
			{
				const Ref<Shader>& lit = shaderLibrary.Get(litKey);
				const bool wantRT = CVars::AnyRTEffectActive();
				// Second axis within the RT variant: the inline ray-query shadow path is dead whenever the
				// stochastic pass supplies a visibility texture instead, so it is compiled out there.
				const bool wantInlineShadows = CVars::LitInlineRTShadowsActive();
				if (!m_LitInitialized || wantRT != m_LastWantRT || wantInlineShadows != m_LastWantInlineShadows)
				{
					const ShaderPermutation desired = wantRT ? ShaderPermutation::Auto : ShaderPermutation::ForceNonRT;
					ShaderDefines dynamic;
					if (wantInlineShadows)
					{
						dynamic.emplace_back("SS_LIT_INLINE_RT_SHADOWS=1");
					}
					if (lit->GetPermutation() != desired || lit->GetDynamicDefines() != dynamic)
					{
						lit->SetPermutation(desired);
						lit->SetDynamicDefines(std::move(dynamic));
						lit->Recompile(); // synchronous; warm .spv cache makes this a fs::exists check after first build
						needPipelineRebuild = true;
						SS_CORE_INFO("DefaultLit permutation -> {}{}", wantRT ? "RT" : "non-RT",
						             wantInlineShadows ? " +inline-shadows" : "");
					}
					m_LastWantRT = wantRT;
					m_LastWantInlineShadows = wantInlineShadows;
					m_LitInitialized = true;
				}
			}
		}

		// render.shaders.debug is startup-only (CVarFlags::ReadOnly): resolved once at launch and keyed into the
		// .spv cache at first compile, so there's no live flip to detect here. Recompiling every shader is a
		// multi-second synchronous stall we deliberately don't do mid-session (Unreal's r.Shaders.Optimize
		// model — the old live checkbox froze the editor). Change it in SnowstormStartup.cfg / CLI and relaunch.

		static float timeSinceLastCheck = 0.0f;
		timeSinceLastCheck += ts.GetSeconds();

		//-- check for source edits every 1 second (hot reload)
		if (timeSinceLastCheck > 1.0f)
		{
			// Recompile any shader whose source (or an included header) changed -> bumps its version and writes
			// fresh SPIR-V to the cache. (ReloadAll self-skips unchanged shaders.)
			shaderLibrary.ReloadAll();
			needPipelineRebuild = true; // a changed shader bumps its version; the sweep below self-skips otherwise
			timeSinceLastCheck = 0.0f;
		}

		if (needPipelineRebuild)
		{
			// Rebuild any live pipeline whose shader version advanced (hot-reload OR the permutation swap above).
			// A pipeline bakes its VkShaderModules at creation, so recompiled SPIR-V is invisible until the
			// VkPipeline is rebuilt from it. Each Reload() self-skips when its shader is unchanged, so this is
			// cheap. Runs in the AssetSync phase (before Render), so no command buffer is open when the pipeline
			// drains the device and swaps its handle.
			Pipeline::ForEachLive([](const Ref<Pipeline>& pipeline)
			                      { pipeline->Reload(); });
		}
	}
}
