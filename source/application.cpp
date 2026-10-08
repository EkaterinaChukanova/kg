#include "application.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>

namespace application {

struct Vertex{
    glm::vec3 position;
    glm::vec3 color;
};

struct UniformBufferObject{
    glm::mat4 mvp;
    glm::vec4 userColor;
};

std::vector<Vertex> generateIcosahedron(); 

VkShaderModule vertShaderModule = VK_NULL_HANDLE;
VkShaderModule fragShaderModule = VK_NULL_HANDLE;

std::vector<char> readFile(const std::string& path){
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return {};
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule createShaderModule(const std::vector<char>& code){
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(graphics::internal::context.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS){
        std::cerr << "Failed to create shader module\n";
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexAllocation = VK_NULL_HANDLE;
uint32_t vertexCount = 0;

VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
VkBuffer uniformBuffer = VK_NULL_HANDLE;
VmaAllocation uniformAllocation = VK_NULL_HANDLE;
void* uniformMapped = nullptr;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

bool initialize() {
    auto& context = graphics::internal::context;

    auto vertCode = readFile("shaders/shader.vert.spv");
    auto fragCode = readFile("shaders/shader.frag.spv");

    vertShaderModule = createShaderModule(vertCode);
    fragShaderModule = createShaderModule(fragCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        std::cerr << "Failed to create shader modules\n";
        return false;
    }

    std::cerr << "Shader modules created\n";

    VkPipelineShaderStageCreateInfo vertStage{};
    VkPipelineShaderStageCreateInfo fragStage{};

    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};

    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Отключить отсечение
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                        VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT |
                                        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout\n";
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS){
        std::cerr << "Failed to create descriptor pool\n";
        return false;
    }

    std::cerr << "Descriptor pool created\n";

    VkDescriptorSetAllocateInfo setAllocateInfo{};
    setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocateInfo.descriptorPool = descriptorPool;
    setAllocateInfo.descriptorSetCount = 1;
    setAllocateInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(context.device, &setAllocateInfo, &descriptorSet) != VK_SUCCESS){
        std::cerr << "Failed to allocate descriptor set\n";
        return false;
    }

    std::cerr << "Descriptor set allocated\n";

    VkBufferCreateInfo uboBufferInfo{};
    uboBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboBufferInfo.size = sizeof(UniformBufferObject);
    uboBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uboBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo uboAllocInfo{};
    uboAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    uboAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;


    VmaAllocationInfo uboAllocResult{};
    if (vmaCreateBuffer(context.allocator, &uboBufferInfo, &uboAllocInfo,
                    &uniformBuffer, &uniformAllocation, &uboAllocResult) != VK_SUCCESS) {
        std::cerr << "Failed to create uniform buffer\n";
        return false;
    }

    auto vertices = generateIcosahedron();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertices.size() * sizeof(Vertex);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vertexCount = static_cast<uint32_t>(vertices.size());
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (vmaCreateBuffer(context.allocator, &bufferInfo, &allocInfo, &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "Failed to create vertex buffer\n";
        return false;
    }
    uniformMapped = uboAllocResult.pMappedData;
    if (uniformMapped == nullptr) {
        std::cerr << "uniformMapped is null!\n";
        return false;
    }
    std::cerr << "Uniform buffer created\n";


    VkDescriptorBufferInfo bufferDescriptorInfo{};
    bufferDescriptorInfo.buffer = uniformBuffer;
    bufferDescriptorInfo.offset = 0;
    bufferDescriptorInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.pBufferInfo = &bufferDescriptorInfo;

    vkUpdateDescriptorSets(context.device, 1, &descriptorWrite, 0, nullptr);
    std::cerr << "Descriptor set updated\n";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout\n";
        return false;
    }

    std::cerr << "Pipeline layout created\n";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = context.render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline\n";
        return false;
    }

    std::cerr << "Graphics pipeline created\n";

    void* data = nullptr;
    vmaMapMemory(context.allocator, vertexAllocation, &data);

    // ИСПОЛЬЗУЙ РЕАЛЬНЫЙ РАЗМЕР ДАННЫХ, А НЕ bufferInfo.size!
    size_t dataSize = vertices.size() * sizeof(Vertex);
    memcpy(data, vertices.data(), dataSize);

    vmaUnmapMemory(context.allocator, vertexAllocation);

    std::cerr << "Copied " << vertices.size() << " vertices (" 
            << dataSize << " bytes) to GPU buffer" << std::endl;
    return true;
}

void shutdown() {
    auto& context = graphics::internal::context;
    if (context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context.device);
    }
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (uniformBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, uniformBuffer, uniformAllocation);
        uniformBuffer = VK_NULL_HANDLE;
        uniformAllocation = VK_NULL_HANDLE;
        uniformMapped = nullptr;
    }

    if (vertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
        vertShaderModule = VK_NULL_HANDLE;
    }
    if (fragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
        fragShaderModule = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, graphicsPipeline, nullptr);
        graphicsPipeline = VK_NULL_HANDLE;
    }
}

void update([[maybe_unused]] double time) {
    if (graphics::internal::context.swapchain_extent.width == 0 ||
        graphics::internal::context.swapchain_extent.height == 0) {
        return;
    }
    ImGui::ShowDemoWindow();
    UniformBufferObject ubo{};
    glm::mat4 model = glm::mat4(1.0f);
    // вращение вокруг Y
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    float aspect = (float)graphics::internal::context.swapchain_extent.width /
                    (float)graphics::internal::context.swapchain_extent.height;
    
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );
    proj[1][1] *= -1.0f;

    ubo.mvp = proj * view * model;
    ubo.userColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    memcpy(uniformMapped, &ubo, sizeof(ubo));
}

std::vector<Vertex> generateIcosahedron() {
    // Канонические координаты вершин икосаэдра через золотое сечение
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    
    glm::vec3 rawPositions[12] = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };

    // Нормализуем к единичной сфере
    glm::vec3 positions[12];
    for (int i = 0; i < 12; ++i) {
        positions[i] = glm::normalize(rawPositions[i]);
    }

    // Проверенные индексы для CCW winding order
    int faces[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    std::vector<Vertex> vertices;
    vertices.reserve(60);

    for (int f = 0; f < 20; ++f) {
        // Уникальный цвет для каждой грани
        float r = ((f * 7) % 10) / 10.0f + 0.2f;
        float g = ((f * 13) % 10) / 10.0f + 0.2f;
        float b = ((f * 17) % 10) / 10.0f + 0.2f;
        glm::vec3 color(r, g, b);

        for (int v = 0; v < 3; ++v) {
            vertices.push_back({positions[faces[f][v]], color});
        }
    }

    return vertices;
}

void render(const graphics::internal::FrameData& fd) {
    if (graphics::internal::context.swapchain_extent.width == 0 ||
        graphics::internal::context.swapchain_extent.height == 0) {
        return;
    }
    // Начинаем запись командного буфера
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    // Начинаем render pass
    auto& context = graphics::internal::context;

    VkClearValue clear_values[2];
    clear_values[0].color = {{0.1f, 0.1f, 0.15f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    const VkRenderPassBeginInfo render_pass_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = { .extent = context.swapchain_extent },
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };

    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Здесь потом можно рисовать (vkCmdBindPipeline, vkCmdDraw и т.д.)
    // Пока оставляем пустым — этого достаточно, чтобы буфер был валидным.

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)context.swapchain_extent.width;
    viewport.height = (float)context.swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context.swapchain_extent;

    VkDeviceSize offset = 0;

    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer, &offset);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(fd.command_buffer, 60, 1, 0, 0);
  // только первый треугольник

    vkCmdEndRenderPass(fd.command_buffer);

    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application