#include "ShaderReloadSystem.hpp"

#include "Snowstorm/Assets/MaterialAsset.hpp"
#include "ShaderReloadRule.hpp"
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
			// Built through the library's own key function, not concatenated: keys are canonicalised now,
			// so a hand-built string would silently miss and this reload would quietly stop working.
			const std::string litKey = ShaderLibrary::MakeKey("Engine/Shaders/Mesh.vert.hlsl", kDefaultFragmentShader);
			if (shaderLibrary.Exists(litKey))
			{
				const Ref<Shader>& lit = shaderLibrary.Get(litKey);
				const bool wantRT = CVars::AnyRTEffectActive();
				// Second axis within the RT variant: the inline ray-query shadow path is dead whenever the
				// stochastic pass supplies a visibility texture instead, so it is compiled out there.
				const bool wantInlineShadows = CVars::LitInlineRTShadowsActive();
				if (LitVariantNeedsCheck(m_Lit, wantRT, wantInlineShadows))
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
					m_Lit = {true, wantRT, wantInlineShadows};
				}
			}
		}

		// render.shaders.debug is startup-only (CVarFlags::ReadOnly): resolved once at launch and keyed into the
		// .spv cache at first compile, so there's no live flip to detect here. Recompiling every shader is a
		// multi-second synchronous stall we deliberately don't do mid-session (Unreal's r.Shaders.Optimize
		// model — the old live checkbox froze the editor). Change it in SnowstormStartup.cfg / CLI and relaunch.

		static float timeSinceLastCheck = 0.0f;

		//-- check for source edits every 1 second (hot reload)
		if (AdvanceReloadTimer(timeSinceLastCheck, ts.GetSeconds()))
		{
			// Recompile any shader whose source (or an included header) changed -> compiled on a worker, which bumps
			// its version once the fresh SPIR-V is published. (ReloadAll self-skips unchanged shaders.)
			shaderLibrary.ReloadAll();
			needPipelineRebuild = true; // a changed shader bumps its version; the sweep below self-skips otherwise
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

	void ShaderReloadSystem::DeclareAccess(SystemAccess& a) const
	{
		a.WriteResources<ShaderLibrary>();
	}
}
