#include "pch.h"

#include "VulkanSwapChain.h"

#include <queue>
#include <GLFW/glfw3.h>

#include "VulkanContext.hpp"
#include "VulkanQueueFamilyIndices.h"

namespace Snowstorm
{
    void VulkanSwapChainQueue::AddDrawCall(const VulkanDrawCallCommand& command)
    {
        m_DrawCalls.emplace(command);
    }

    VulkanDrawCallCommand VulkanSwapChainQueue::GetNextDrawCall()
    {
        SS_CORE_ASSERT(!m_DrawCalls.empty(), "No draw calls available!");

        auto nextDrawCall = m_DrawCalls.front();
        m_DrawCalls.pop();

        return nextDrawCall;
    }

    void VulkanSwapChainQueue::EnqueueUniformBufferValue(const std::string& name, const void* data, uint32_t bufferSize)
    {
        if (!m_UniformBuffers.contains(name))
        {
            m_UniformBuffers.emplace(name, CreateRef<VulkanUniformBuffer>(bufferSize));
        }

        m_UniformBuffers[name]->EnqueueData(data, bufferSize);
    }

    Ref<VulkanUniformBuffer> VulkanSwapChainQueue::GetUniformBuffer(const std::string& name)
    {
        SS_CORE_ASSERT(m_UniformBuffers.contains(name), "Uniform buffer not found!");
        return m_UniformBuffers[name];
    }

    void VulkanSwapChainQueue::SetDescriptorSetLayout(const std::string& name,
                                                      const Ref<VulkanDescriptorSetLayout>& descriptorSetLayout)
    {
        m_DescriptorSets.emplace(name, descriptorSetLayout);
    }

    Ref<VulkanDescriptorSetLayout> VulkanSwapChainQueue::GetDescriptorSetLayout(const std::string& name)
    {
        SS_CORE_ASSERT(m_DescriptorSets.contains(name), "Uniform buffer not found!");
        return m_DescriptorSets[name];
    }

    VulkanSwapChain::VulkanSwapChain(const VkPhysicalDevice physicalDevice, const VkDevice device,
                                     const VkSurfaceKHR surface,
                                     GLFWwindow* window)
        : m_Device(device), m_PhysicalDevice(physicalDevice), m_Surface(surface), m_Window(window)
    {
        CreateSwapChain();

        // Create render pass
        m_RenderPass = CreateScope<VulkanRenderPass>(m_Device, m_SwapChainImageFormat);

        // Create graphics pipeline
        m_GraphicsPipeline = CreateScope<VulkanGraphicsPipeline>(m_Device, *m_RenderPass);

        // Create image views
        CreateImageViews();
        CreateFramebuffers();
    }

    VulkanSwapChain::~VulkanSwapChain()
    {
        CleanupSwapChain();
    }

    void VulkanSwapChain::RecreateSwapChain()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);
        while (width == 0 || height == 0)
        {
            // if width or height is 0 -> the window is minimized, so do nothing here
            glfwGetFramebufferSize(m_Window, &width, &height);
            glfwWaitEvents();
        }

        // we shouldn't touch resources that may still be in use
        vkDeviceWaitIdle(m_Device);

        // make sure the old versions of these objects are cleaned up first
        CleanupSwapChain();

        // we won't recreate the render pass, technically we may need to if moving to a different monitor,
        // but we won't take this into account
        CreateSwapChain();
        CreateImageViews();
        CreateFramebuffers();
    }

    void VulkanSwapChain::RecordCommandBuffer(const VkCommandBuffer commandBuffer, const uint32_t imageIndex,
                                              const VkDescriptorSet& descriptorSet) const
    {
        // writes the commands from the command buffer to the swap chain image tied to the index 
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0; // Optional - not relevant right now
        beginInfo.pInheritanceInfo = nullptr; // Optional - only relevant for secondary command buffers

        VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!")

        // begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *m_RenderPass;
        renderPassInfo.framebuffer = m_SwapChainFramebuffers[imageIndex];

        // define the size of the render area - match the size of the attachment
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;

        // clear values to use for VK_ATTACHMENT_LOAD_OP_CLEAR
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &s_ClearValue;

        // begin the render pass
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        // inline - embedded in the primary command buffer itself, with no secondary command buffers 

        // bind the graphics pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *m_GraphicsPipeline);

        // viewport and scissor states are dynamic - set them here
        // viewport - the region of the framebuffer that the output will be rendered to
        // always (0, 0) to (width, height)
        // here we define the transformation from the image to the framebuffer
        vkCmdSetViewport(commandBuffer, 0, 1, &s_Viewport);

        // next define the scissor rectangle -> basically works like a filter
        VkRect2D scissor{};
        scissor.offset = {0, 0}; // we want the whole image
        scissor.extent = m_SwapChainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // bind the vertex buffer during rendering operations
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *m_GraphicsPipeline);

        // TODO see if this works
        VulkanSwapChainQueue* swapChainQueue = VulkanSwapChainQueue::GetInstance();

        while (!swapChainQueue->IsEmpty())
        {
            // get next vertex array
            auto [vertexArray, indexCount, uniformBufferObject] = swapChainQueue->GetNextDrawCall();

            // bind the uniform buffer data
            VulkanContext::SubmitUniformBufferObject(uniformBufferObject);

            std::vector<VkBuffer> vertexBuffers;

            vertexBuffers.reserve(vertexArray->GetVertexBuffers().size());
            for (auto& vertexBuffer : vertexArray->GetVertexBuffers())
            {
                vertexBuffers.push_back(reinterpret_cast<VkBuffer>(vertexBuffer->GetHandle()));
            }

            constexpr VkDeviceSize offsets[] = {0}; // offsets to start reading vertex data from
            // bind vertex buffers to bindings
            vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets);

            // we have to bind the index buffer as well
            vkCmdBindIndexBuffer(commandBuffer, reinterpret_cast<VkBuffer>(vertexArray->GetIndexBuffer()->GetHandle()),
                                 0,
                                 VK_INDEX_TYPE_UINT32);
            // You can only have a SINGLE index buffer! not possible to use different indices for each vertex attribute

            // finally, issue the draw command
            // vkCmdDraw(commandBuffer, vertices.size(), 1, 0, 0);

            // change the drawing command to drawIndexed to utilize the index buffer
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_GraphicsPipeline->GetPipelineLayout(), 0, 1,
                                    &descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
            // first index - offset in the index buffer
            // 1 instance - we are not using instancing
            // TODO allocate multiple resources like buffers from a single memory allocation
            // TODO and store multiple buffers into a single VkBuffer and use offsets in commands like vkCmdBindVertexBuffers
            // TODO the data is more cache friendly this way! (this is known as aliasing)

            // firstVertex: lowest value of gl_VertexIndex
            // firstInstance: offset for instanced rendering, lowest value of gl_InstanceIndex
        }

        // end the render pass and finish recording the command buffer
        vkCmdEndRenderPass(commandBuffer);

        result = vkEndCommandBuffer(commandBuffer);
        SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to record command buffer!");
    }

    VulkanSwapChainSupportDetails VulkanSwapChain::QuerySwapChainSupport(
        const VkPhysicalDevice device, const VkSurfaceKHR surface)
    {
        VulkanSwapChainSupportDetails details;

        // device and surface -> core components of the swap chain
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        // query available surface formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

        if (formatCount != 0)
        {
            details.formats.resize(formatCount); // vector resize to hold all the formats
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                                 details.formats.data());
        }

        // query available presentation modes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

        if (presentModeCount != 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount,
                                                      details.presentModes.data());
        }

        return details;
    }


    VkSurfaceFormatKHR VulkanSwapChain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& availableFormat : availableFormats)
        {
            // this is optimal -> we always want sRGB
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }

        // if it fails, ok to settle with the first format available
        return availableFormats[0];
    }

    VkPresentModeKHR VulkanSwapChain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        // VK_PRESENT_MODE_FIFO_KHR -> always guaranteed -> this is vertical sync (use on mobile phones)
        // VK_PRESENT_MODE_MAILBOX_KHR -> triple buffering -> good if energy expenditure is not an issue

        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanSwapChain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
    {
        // swap extent - resolution of the swap chain -> (almost always) equal to the
        // resolution of the window we are drawing in -> in pixels
        // width and height -> in currentExtent
        // we can differ here sometimes -> set the currentExtent to the max value of uint32_t
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent; // we want to match it here
        }

        // certain windows managers allow differing values
        int width, height;
        glfwGetFramebufferSize(window, &width, &height); // query the resolution of the window in pixel

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // match against the minimum and maximum image extent
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }

    void VulkanSwapChain::CleanupSwapChain() const
    {
        for (const auto& swapChainFramebuffer : m_SwapChainFramebuffers)
        {
            vkDestroyFramebuffer(m_Device, swapChainFramebuffer, nullptr);
        }

        for (const auto& swapChainImageView : m_SwapChainImageViews)
        {
            vkDestroyImageView(m_Device, swapChainImageView, nullptr);
        }

        vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
    }

    void VulkanSwapChain::CreateSwapChain()
    {
        const VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice, m_Surface);

        const VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
        const VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
        const VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities, m_Window);

        // recommended to request at least one more image than the minimum -> else we have to wait for internal operations
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

        // also do not exceed the maximum number of images while doing this (0 is a special value - there is no max)
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        // Now create the actual swap chain
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

        // first specify the surface the swap chain should be tied to
        createInfo.surface = m_Surface;

        // details of the swap chain images go next
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;

        // amount of layers each image consists of -> always 1 unless with a stereoscopic 3D app
        createInfo.imageArrayLayers = 1;

        // what kind of operations we'll use the images for -> we render directly to them here
        // also possible: rendering images to a separate image first, then do post-processing (VK_IMAGE_USAGE_TRANSFER_DST_BIT), then transfer to a swap chain image
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Next specify how to handle swap chain images used across multiple queue families
        // This is the case when the graphics queue family is different from the presentation queue
        const VulkanQueueFamilyIndices indices = VulkanQueueFamilyIndices::FindQueueFamilies(
            m_PhysicalDevice, m_Surface);

        const uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        // draw from the graphics queue -> then submit to the presentation queue
        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            // images can be used concurrently across multiple queue families
            // TODO we will do ownership transfer later

            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            // best performance - explicit ownership transfer

            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        // if we want to do a pre-transform, like rotating by 90 degrees, etc.
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

        // specifies if the alpha channel should be used for blending with other windows in the window system
        // we almost always want to ignore this
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        // if clipped is VK_TRUE -> we don't care about the color of pixels that are obscured (if another window is in front for example)
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        // old swap chain -> with Vulkan it is possible that another swap chain becomes invalid/unoptimized (if the window is resized)
        // in this case, the swap chain needs to be recreated from scratch!
        // TODO we will do this in the future, for now, only assume one swap chain
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        const VkResult result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain);
        SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create swap chain!");

        // we only specified a minimum number of images -> the implementation is allowed to create a swap chain with more
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
        m_SwapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

        // store these for later
        m_SwapChainImageFormat = surfaceFormat.format;
        m_SwapChainExtent = extent;
    }

    void VulkanSwapChain::CreateImageViews()
    {
        m_SwapChainImageViews.resize(m_SwapChainImages.size());

        for (size_t i = 0; i < m_SwapChainImages.size(); i++)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_SwapChainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_SwapChainImageFormat;

            // stick to the default mappings -> we can do stuff like monochrome here instead
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // subresource range -> what the image's purpose is and which part of the image should be accessed
            // we don't need any mipmapping levels or multiple layers
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            // stereographic 3D application -> would have multiple layers (one for each eye)

            const VkResult result = vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapChainImageViews[i]);
            SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create image views!")
        }
    }

    void VulkanSwapChain::CreateFramebuffers()
    {
        m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());

        for (size_t i = 0; i < m_SwapChainImageViews.size(); i++)
        {
            const VkImageView attachments[] = {
                m_SwapChainImageViews[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = *m_RenderPass;
            // you can only use a framebuffer with the compatible render pass

            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments; // binds it to the pAttachment of the render pass
            framebufferInfo.width = m_SwapChainExtent.width;
            framebufferInfo.height = m_SwapChainExtent.height;
            framebufferInfo.layers = 1;

            const VkResult result = vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr,
                                                        &m_SwapChainFramebuffers[i]);
            SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create framebuffer!");
        }
    }
}
