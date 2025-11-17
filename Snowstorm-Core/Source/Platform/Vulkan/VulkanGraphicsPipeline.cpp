#include "pch.h"
#include "VulkanGraphicsPipeline.h"

#include "VulkanSwapChain.h"
#include "VulkanVertexArray.h"

namespace Snowstorm
{
    namespace
    {
        std::vector<char> ReadFile(const std::string& filename)
        {
            // ate: start reading at the end of the file
            std::ifstream file(filename, std::ios::ate | std::ios::binary);

            SS_CORE_ASSERT(file.is_open(), "Failed to open file!");

            // we can use the end position to tell the size of the buffer
            const size_t fileSize = file.tellg();
            std::vector<char> buffer(fileSize);

            // seek back to the beginning and load the data
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(fileSize));

            file.close();

            return buffer;
        }

        VkShaderModule CreateShaderModule(const VkDevice device, const std::vector<char>& code)
        {
            // std::vector -> the default allocator ensures that the data satisfies the worst case alignment requirements
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

            VkShaderModule shaderModule;
            const VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
            SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create shader module!");

            return shaderModule;
        }
    }

    VulkanGraphicsPipeline::VulkanGraphicsPipeline(VkDevice device, VkRenderPass renderPass)
        : m_Device(device)
    {
        // load the code
        // TODO move this to shader
        const auto vertShaderCode = ReadFile("Assets/Shaders/vert.spv");
        const auto fragShaderCode = ReadFile("Assets/Shaders/frag.spv");

        // create the shaders with a wrapper
        const VkShaderModule vertShaderModule = CreateShaderModule(device, vertShaderCode);
        const VkShaderModule fragShaderModule = CreateShaderModule(device, fragShaderCode);

        // assign the actual shaders to a specific pipeline stage
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; // enum for each programmable stage
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main"; // entry point (we can combine multiple shaders into a single file this way)
        // pSpecializationInfo -> you can specify values for shader constants

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // describe the format of the vertex data that will be passed to the vertex shader
        // bindings: spacing between data and whether the data is per-vertex or per-instance
        // attribute descriptions: type of the attributes passed to the vertex shader, which binding
        // to load them from and at which offset
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        auto bindingDescription = VulkanVertexArray::GetBindingDescription();
        auto attributeDescriptions = VulkanVertexArray::GetAttributeDescriptions();

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // next define the topology - we intend to draw triangles so stick to this structure
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // triangle from every 3 vertices without reuse
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        // with an element buffer - specify indices to use yourself (reuse vertices - optimization)


        // setup the dynamic state, we can change and must specify these properties without recreating the pipeline from scratch
        // at runtime we can change the viewport and teh scissor state
        const std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // after this only need to specify their count at pipeline creation time
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        // the actual viewport(s) and scissor rectangle(s) will then later be set up at drawing time
        // using multiple viewports and scissor rectangles requires enabling a GPU feature

        // rasterizer - turns geometry into fragments to be colored by the fragment shader
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

        rasterizer.depthClampEnable = VK_FALSE;
        // if set to VK_TRUE, the fragments beyond the near and far planes are clamped as opposed to discarded
        // useful in some place like shadow maps (need to enable a GPU feature for this)

        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        // if set to true, the geometry never passes through the rasterizer stage (disables output to framebuffer)

        rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // fill the area of the polygon with fragments (fill)
        // determines how fragments are generated for geometry
        // any other mode requires a GPU feature

        rasterizer.lineWidth = 1.0f;
        // thickness of line in terms of number of fragments
        // any line thicker than 1.0f requires you to enable the wideLines GPU feature

        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        // cullMode - type of face culling to use
        // frontFace - vertex order for faces to be considered front-facing

        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // optional
        rasterizer.depthBiasClamp = 0.0f; // optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // optional
        // altering depth values by adding a constant or biasing based on the fragment slope - we won't use
        // sometimes used for shadow mapping

        // multisampling - one way to perform anti-aliasing
        // combines the fragment shader results of multiple polygons that rasterize to the same pixel
        // mostly along the edges
        // not expensive - doesn't need to run the fragment shader multiple times if only one polygon maps to a pixel
        // enabling it requires enabling a GPU feature
        // TODO we will revisit this later
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        // if using depth/stencil buffer, need to configure the depth and stencil tests
        // using VkPipelineDepthStencilStateCreateInfo, we don't have one right now, so we can pass nullptr

        // Color blending
        // after a fragment shader returns a color, it needs to be combined with the color that is already in the
        // framebuffer -> color blending
        // Two ways: 1. mix the old and new value; 2. combine using bitwise operation 
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        // there is another global struct (this one is tied to framebuffer), but we have only one framebuffer
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // write mask - to determine which channels are passed through -  in this case, all of them

        // alpha blending:
        // finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
        // finalColor.a = newAlpha * a;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        // global blend factors/constants
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE; // if we were to use bitwise combinations
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment; // array of attachments here for all the framebuffers
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        // uniform values - to be specified by the VkPipelineLayout object
        CreateDescriptorSetLayout(); // create descriptor set layouts first

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        VkDescriptorSetLayout descriptorSetLayout = *m_DescriptorSetLayout;
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional
        // push constants -> another way of passing dynamic values to shad

        VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout);
        SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout!");

        // finally, create the graphics pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};

        // reference the array of structs
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;

        // reference all the structures describing the fixed-function state
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr; // Optional
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;

        // reference the pipeline layout - Vulkan handle
        pipelineInfo.layout = m_PipelineLayout;

        // reference to the render pass and the index of the subpass where this pipeline is used
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        // we can also use other render passes with this pipeline IF COMPATIBLE
        // we won't be using this now

        // we can derive from an existing pipeline -> less expensive than creating from scratch
        // right now there is only a single pipeline
        // only used if the VK_PIPELINE_CREATE_DERIVATIVE_BIT is set in VkGraphicsPipelineCreateInfo
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        // this function can actually create multiple pipelines immediately
        // pipeline cache - for storing and reusing data relevant to pipeline creation across multiple calls
        // TODO we will use a pipeline cache later
        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                           &m_GraphicsPipeline);
        SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline!");

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        s_CurrentPipeline = this;
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
    {
        vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    }

    void VulkanGraphicsPipeline::CreateDescriptorSetLayout()
    {
        // TODO make this actually parameterized
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

        // TODO return this
        // VkDescriptorSetLayoutBinding texLayoutBinding{};
        // texLayoutBinding.binding = 1;
        // texLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // texLayoutBinding.descriptorCount = 32;
        // texLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        // texLayoutBinding.pImmutableSamplers = nullptr; // Optional

        std::vector bindings = {uboLayoutBinding}; //, texLayoutBinding};
        m_DescriptorSetLayout = CreateRef<VulkanDescriptorSetLayout>(m_Device, bindings);
    }
}
