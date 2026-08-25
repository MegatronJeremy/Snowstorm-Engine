#include "pch.h"

#include "VulkanPipelineStats.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "VulkanContext.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace Snowstorm::VulkanPipelineStats
{
	namespace
	{
		struct Entry
		{
			std::string Pipeline;
			std::string Executable;
			std::string Description;
			uint32_t Subgroup = 0;
			VkShaderStageFlags Stages = 0;
			nlohmann::json Stats = nlohmann::json::object();
		};

		std::mutex g_Mutex;
		std::vector<Entry> g_Entries;

		// The stage set is a bitmask, and a single executable can cover several stages once a driver merges
		// them (NVIDIA regularly merges VS+GS). Spelling the mask out keeps the JSON readable without the
		// consumer needing Vulkan's bit values.
		std::string StageNames(const VkShaderStageFlags flags)
		{
			static constexpr struct
			{
				VkShaderStageFlagBits Bit;
				const char* Name;
			} kStages[] = {
			    {VK_SHADER_STAGE_VERTEX_BIT, "vertex"},
			    {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "tess_control"},
			    {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "tess_eval"},
			    {VK_SHADER_STAGE_GEOMETRY_BIT, "geometry"},
			    {VK_SHADER_STAGE_FRAGMENT_BIT, "fragment"},
			    {VK_SHADER_STAGE_COMPUTE_BIT, "compute"},
			};
			std::string out;
			for (const auto& [bit, name] : kStages)
			{
				if ((flags & bit) == 0)
				{
					continue;
				}
				if (!out.empty())
				{
					out += "+";
				}
				out += name;
			}
			return out.empty() ? "unknown" : out;
		}
	}

	void Record(const VkDevice device, const VkPipeline pipeline, const std::string& name)
	{
		if (!CVars::ShaderStats.Get() || !VulkanContext::Get().SupportsPipelineStats() || pipeline == VK_NULL_HANDLE)
		{
			return;
		}

		VkPipelineInfoKHR pipeInfo{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
		pipeInfo.pipeline = pipeline;

		uint32_t execCount = 0;
		if (vkGetPipelineExecutablePropertiesKHR(device, &pipeInfo, &execCount, nullptr) != VK_SUCCESS || execCount == 0)
		{
			return;
		}
		std::vector<VkPipelineExecutablePropertiesKHR> execs(
		    execCount, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
		if (vkGetPipelineExecutablePropertiesKHR(device, &pipeInfo, &execCount, execs.data()) != VK_SUCCESS)
		{
			return;
		}

		for (uint32_t i = 0; i < execCount; ++i)
		{
			VkPipelineExecutableInfoKHR execInfo{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
			execInfo.pipeline = pipeline;
			execInfo.executableIndex = i;

			uint32_t statCount = 0;
			if (vkGetPipelineExecutableStatisticsKHR(device, &execInfo, &statCount, nullptr) != VK_SUCCESS ||
			    statCount == 0)
			{
				continue;
			}
			std::vector<VkPipelineExecutableStatisticKHR> stats(
			    statCount, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
			if (vkGetPipelineExecutableStatisticsKHR(device, &execInfo, &statCount, stats.data()) != VK_SUCCESS)
			{
				continue;
			}

			Entry e;
			e.Pipeline = name;
			e.Executable = execs[i].name;
			e.Description = execs[i].description;
			e.Subgroup = execs[i].subgroupSize;
			e.Stages = execs[i].stages;
			for (const auto& s : stats)
			{
				// The value is a union discriminated by `format`, so the declared format is recorded next to
				// the value rather than dropped. A driver that populates a different arm than it declares
				// produces a number that looks plausible and is not, and without the format the consumer
				// cannot tell: the RTX 5070 reports "Local Memory Size" as uint64 and returns a constant
				// 2^36 (low 32 bits zero) for every executable, which is a discriminator artifact, not a
				// 64 GiB spill. Keeping the format lets Scripts/shader-stats.py reject such a column
				// instead of a reader mistaking it for data.
				switch (s.format)
				{
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
					e.Stats[s.name] = {{"format", "bool32"}, {"value", s.value.b32 != VK_FALSE}};
					break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
					e.Stats[s.name] = {{"format", "int64"}, {"value", s.value.i64}};
					break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
					e.Stats[s.name] = {{"format", "uint64"}, {"value", s.value.u64}};
					break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
					e.Stats[s.name] = {{"format", "float64"}, {"value", s.value.f64}};
					break;
				default:
					e.Stats[s.name] = {{"format", "unknown"}, {"value", nullptr}};
					break;
				}
			}

			std::lock_guard lock(g_Mutex);
			g_Entries.push_back(std::move(e));
		}
	}

	void Write()
	{
		std::lock_guard lock(g_Mutex);
		if (!CVars::ShaderStats.Get() || g_Entries.empty())
		{
			return;
		}

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(VulkanContext::Get().GetPhysicalDevice(), &props);

		nlohmann::json root;
		root["device"] = props.deviceName;
		root["source"] = "VK_KHR_pipeline_executable_properties";
		auto& arr = root["executables"] = nlohmann::json::array();
		for (const auto& e : g_Entries)
		{
			arr.push_back({{"pipeline", e.Pipeline},
			               {"executable", e.Executable},
			               {"description", e.Description},
			               {"stages", StageNames(e.Stages)},
			               {"subgroupSize", e.Subgroup},
			               {"stats", e.Stats}});
		}

		const std::filesystem::path out = CVars::ShaderStatsPath.Get();
		if (out.has_parent_path())
		{
			std::error_code ec;
			create_directories(out.parent_path(), ec);
		}
		if (std::ofstream f(out); f)
		{
			f << root.dump(1, '\t') << "\n";
			SS_CORE_INFO("shader.stats: wrote {} executable(s) to {}", g_Entries.size(), out.string());
		}
		else
		{
			SS_CORE_ERROR("shader.stats: could not open {} for writing.", out.string());
		}
		g_Entries.clear();
	}
}
