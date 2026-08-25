#pragma once

#include <volk.h>

#include <string>

// Per-shader statistics reported by the DRIVER's compiler, via VK_KHR_pipeline_executable_properties.
//
// This is the vendor-neutral counterpart to Scripts/rga-occupancy.py. RGA is the Radeon GPU Analyzer and
// only reads AMD ISA, so it cannot see the NVIDIA half of a cross-vendor comparison; this asks whichever
// driver actually compiled the pipeline. What each vendor reports differs (AMD gives VGPRs/SGPRs/LDS and
// an occupancy figure, NVIDIA gives register count and spill counts), so the output is a free-form list of
// named values rather than a fixed schema, and the consumer decides what it can compare.
//
// Not a replacement for RGA: RGA is static and runs with no GPU, so it stays the CI gate. This needs the
// device that will run the shader, which is exactly why it can answer the cross-vendor question.
namespace Snowstorm::VulkanPipelineStats
{
	// Query and stash the statistics for a freshly created pipeline. No-op unless shader.stats is set AND
	// the device enabled the extension, so the normal path costs one bool test.
	void Record(VkDevice device, VkPipeline pipeline, const std::string& name);

	// Write everything recorded so far to shader.stats.path. No-op when nothing was recorded.
	void Write();
}
