#pragma once

#include "VulkanCommon.hpp"

#include <mutex>

namespace Snowstorm
{
	class VulkanBindlessManager
	{
	public:
		void Init();
		void Shutdown();

		static VulkanBindlessManager& Get();

		//-- Assigns a global index and updates the descriptor set immediately.
		// RegisterTexture -> binding 0 (Texture2D[]); RegisterCube -> binding 1 (TextureCube[]). Cube views
		// can't live in the 2D array because HLSL types differ (Texture2D vs TextureCube), so they get a
		// parallel binding in the same set. Both are VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE (view-type agnostic
		// at the Vulkan layer); the cube-ness comes from the image view's VK_IMAGE_VIEW_TYPE_CUBE.
		uint32_t RegisterTexture(VkImageView imageView);
		uint32_t RegisterCube(VkImageView imageView);

		// Repoint an already-allocated 2D texture slot at a different image view (UPDATE_AFTER_BIND makes
		// this legal while the set is bound). Used by async texture loading: a slot is first registered with
		// a placeholder view, then rewritten to the real texture when its decode+upload finishes — the slot
		// index (baked into material constants) never changes, so no material needs patching. Main thread.
		void WriteTexture(uint32_t index, VkImageView imageView);

		// Point binding 2 (the single TLAS slot) at the scene's acceleration structure (#118). Ray-query
		// shaders read it via the bindless set. VK_NULL_HANDLE clears it (no scene AS). Only present when the
		// device supports RT (the binding is created conditionally in Init); no-op otherwise. Main thread.
		void WriteAccelerationStructure(VkAccelerationStructureKHR tlas);

		[[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
		[[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }

		static constexpr uint32_t MAX_BINDLESS_TEXTURES = 10000;
		static constexpr uint32_t MAX_BINDLESS_CUBES = 256;

		static constexpr uint32_t BINDING_TEXTURE2D = 0;
		static constexpr uint32_t BINDING_TEXTURECUBE = 1;
		static constexpr uint32_t BINDING_TLAS = 2; // single-slot acceleration structure (#118; RT only)

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

		bool m_RayTracing = false; // binding 2 (TLAS) present only when the device supports RT

		uint32_t m_NextFreeIndex = 0;
		uint32_t m_NextFreeCubeIndex = 0;
		std::mutex m_IndexMutex;
	};
}
