#pragma execution_character_set("utf-8")
#include "application.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

extern GLFWwindow* glfw_window;

namespace application {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

// Структура свойств одной модели (для динамического UBO)
struct ModelUBO {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 cameraPos;
    glm::vec4 specColor;
    glm::vec4 ambient;
};

constexpr int MAX_OBJECTS = 3;

glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 6.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

glm::vec4 g_lightColor = glm::vec4(1.0f);
glm::vec4 g_lightDir = glm::vec4(0.0f, -0.3f, -1.0f, 0.0f);
glm::vec4 g_specColor = glm::vec4(1.0f, 1.0f, 1.0f, 32.0f);
glm::vec4 g_ambient = glm::vec4(0.06f, 0.06f, 0.06f, 0.0f);
float g_intensity = 1.0f;
float g_fov = 45.0f;

float yaw = -90.0f;
float pitch = 0.0f;

std::vector<Vertex> generateIcosahedron();

VkShaderModule vertShaderModule = VK_NULL_HANDLE;
VkShaderModule fragShaderModule = VK_NULL_HANDLE;

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << "\n";
        return {};
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(graphics::internal::context.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create shader module\n";
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

// Vertex buffer (DEVICE_LOCAL)
VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexAllocation = VK_NULL_HANDLE;
uint32_t vertexCount = 0;

// Dynamic uniform buffer
VkBuffer uniformBuffer = VK_NULL_HANDLE;
VmaAllocation uniformAllocation = VK_NULL_HANDLE;
void* uniformMapped = nullptr;
VkDeviceSize dynamicAlignment = 0;
VkDeviceSize uniformBufferSize = 0;

// Texture
VkImage textureImage = VK_NULL_HANDLE;
VmaAllocation textureAllocation = VK_NULL_HANDLE;
VkImageView textureImageView = VK_NULL_HANDLE;
VkSampler textureSampler = VK_NULL_HANDLE;

// Descriptor
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

// Временный command pool для staging-копий
VkCommandPool stagingCommandPool = VK_NULL_HANDLE;

VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = stagingCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(graphics::internal::context.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphics::internal::context.graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics::internal::context.graphics_queue);

    vkFreeCommandBuffers(graphics::internal::context.device, stagingCommandPool, 1, &commandBuffer);
}

void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);
    endSingleTimeCommands(cmd);
}

bool createTextureImage(const char* path) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return false;
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4;

    // Staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingAllocResult{};
    if (vmaCreateBuffer(graphics::internal::context.allocator, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAlloc, &stagingAllocResult) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return false;
    }

    memcpy(stagingAllocResult.pMappedData, pixels, imageSize);
    vmaFlushAllocation(graphics::internal::context.allocator, stagingAlloc, 0, imageSize);
    stbi_image_free(pixels);

    // Image in VRAM
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    imageAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(graphics::internal::context.allocator, &imageInfo, &imageAllocInfo,
                       &textureImage, &textureAllocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(graphics::internal::context.allocator, stagingBuffer, stagingAlloc);
        return false;
    }

    // Transition + copy
    VkCommandBuffer cmd = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(cmd);

    vmaDestroyBuffer(graphics::internal::context.allocator, stagingBuffer, stagingAlloc);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(graphics::internal::context.device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        return false;
    }

    // Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(graphics::internal::context.device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool initialize() {
    auto& context = graphics::internal::context;

    // Staging command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.graphics_queue_index;
    if (vkCreateCommandPool(context.device, &poolInfo, nullptr, &stagingCommandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create staging command pool\n";
        return false;
    }

    auto vertCode = readFile("shaders/shader.vert.spv");
    auto fragCode = readFile("shaders/shader.frag.spv");

    vertShaderModule = createShaderModule(vertCode);
    fragShaderModule = createShaderModule(fragCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        return false;
    }

    // Загрузка текстуры (положи файл textures/texture.png)
    if (!createTextureImage("textures/texture.png")) {
        std::cerr << "Texture load failed. Put any PNG as textures/texture.png\n";
        return false;
    }

    // Dynamic UBO alignment
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context.physical_device, &props);
    dynamicAlignment = props.limits.minUniformBufferOffsetAlignment;
    if (dynamicAlignment > 0) {
        dynamicAlignment = (sizeof(ModelUBO) + dynamicAlignment - 1) & ~(dynamicAlignment - 1);
    } else {
        dynamicAlignment = sizeof(ModelUBO);
    }
    uniformBufferSize = MAX_OBJECTS * dynamicAlignment;

    // Descriptor set layout
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Descriptor pool
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = 2;
    poolCreateInfo.pPoolSizes = poolSizes;
    poolCreateInfo.maxSets = 1;
    if (vkCreateDescriptorPool(context.device, &poolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = descriptorPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(context.device, &setAllocInfo, &descriptorSet) != VK_SUCCESS) {
        return false;
    }

    // Dynamic uniform buffer (host visible)
    VkBufferCreateInfo uboInfo{};
    uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboInfo.size = uniformBufferSize;
    uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo uboAllocInfo{};
    uboAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    uboAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo uboAllocResult{};
    if (vmaCreateBuffer(context.allocator, &uboInfo, &uboAllocInfo,
                        &uniformBuffer, &uniformAllocation, &uboAllocResult) != VK_SUCCESS) {
        return false;
    }
    uniformMapped = uboAllocResult.pMappedData;

    // Vertex buffer → staging → DEVICE_LOCAL
    auto vertices = generateIcosahedron();
    vertexCount = static_cast<uint32_t>(vertices.size());
    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(Vertex);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = vertexBufferSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocCreate{};
    stagingAllocCreate.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCreate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingResult{};
    if (vmaCreateBuffer(context.allocator, &stagingBufInfo, &stagingAllocCreate,
                        &stagingBuffer, &stagingAlloc, &stagingResult) != VK_SUCCESS) {
        return false;
    }
    memcpy(stagingResult.pMappedData, vertices.data(), vertexBufferSize);
    vmaFlushAllocation(context.allocator, stagingAlloc, 0, vertexBufferSize);

    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = vertexBufferSize;
    vbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vbAllocInfo{};
    vbAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vbAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT; // DEVICE_LOCAL

    if (vmaCreateBuffer(context.allocator, &vbInfo, &vbAllocInfo,
                        &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(context.allocator, stagingBuffer, stagingAlloc);
        return false;
    }

    copyBuffer(stagingBuffer, vertexBuffer, vertexBufferSize);
    vmaDestroyBuffer(context.allocator, stagingBuffer, stagingAlloc);

    // Write descriptors
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(ModelUBO);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &bufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

    // Pipeline
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[3]{};
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }

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

    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void shutdown() {
    auto& context = graphics::internal::context;
    if (context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context.device);
    }

    if (stagingCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context.device, stagingCommandPool, nullptr);
        stagingCommandPool = VK_NULL_HANDLE;
    }

    if (textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(context.device, textureSampler, nullptr);
        textureSampler = VK_NULL_HANDLE;
    }
    if (textureImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, textureImageView, nullptr);
        textureImageView = VK_NULL_HANDLE;
    }
    if (textureImage != VK_NULL_HANDLE) {
        vmaDestroyImage(context.allocator, textureImage, textureAllocation);
        textureImage = VK_NULL_HANDLE;
        textureAllocation = VK_NULL_HANDLE;
    }

    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;
    }
    if (uniformBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, uniformBuffer, uniformAllocation);
        uniformBuffer = VK_NULL_HANDLE;
        uniformAllocation = VK_NULL_HANDLE;
        uniformMapped = nullptr;
    }

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
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

    ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 3 - Texture (base)");

    if (ImGui::CollapsingHeader("Камера", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Позиция", &cameraPos.x, 0.05f);
        ImGui::SliderFloat("Поворот (yaw)", &yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Наклон (pitch)", &pitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Угол обзора", &g_fov, 10.0f, 120.0f);
    }
    if (ImGui::CollapsingHeader("Направленный свет", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Цвет света", &g_lightColor.x);
        ImGui::SliderFloat("Интенсивность", &g_intensity, 0.0f, 5.0f);
        ImGui::SliderFloat3("Направление", &g_lightDir.x, -1.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Материал", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Цвет блика", &g_specColor.x);
        ImGui::SliderFloat("Коэффициент блика", &g_specColor.w, 1.0f, 128.0f);
        ImGui::ColorEdit3("Рассеянный свет", &g_ambient.x);
        ImGui::Text("Albedo берётся из текстуры");
    }
    ImGui::End();

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);

    float cameraSpeed = 0.05f;
    if (glfwGetKey(glfw_window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(glfw_window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(glfw_window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    float aspect = (float)graphics::internal::context.swapchain_extent.width /
                   (float)graphics::internal::context.swapchain_extent.height;
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(g_fov), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    glm::vec3 dir(g_lightDir);
    if (glm::dot(dir, dir) < 1e-6f)
        dir = glm::vec3(0.0f, 0.0f, -1.0f);

    // 3 объекта с разными позициями
    glm::vec3 positions[MAX_OBJECTS] = {
        glm::vec3(-2.0f, 0.0f, 0.0f),
        glm::vec3( 0.0f, 0.0f, 0.0f),
        glm::vec3( 2.0f, 0.0f, 0.0f)
    };

    for (int i = 0; i < MAX_OBJECTS; ++i) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[i]);
        model = glm::rotate(model, static_cast<float>(time) * (0.5f + i * 0.3f), glm::vec3(0.0f, 1.0f, 0.0f));

        ModelUBO ubo{};
        ubo.model = model;
        ubo.view = view;
        ubo.proj = proj;
        ubo.mvp = proj * view * model;
        ubo.lightDir = glm::vec4(glm::normalize(dir), 0.0f);
        ubo.lightColor = glm::vec4(glm::vec3(g_lightColor), g_intensity);
        ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
        ubo.specColor = g_specColor;
        ubo.ambient = g_ambient;

        char* dst = static_cast<char*>(uniformMapped) + i * dynamicAlignment;
        memcpy(dst, &ubo, sizeof(ModelUBO));
    }
    vmaFlushAllocation(graphics::internal::context.allocator, uniformAllocation, 0, uniformBufferSize);
}

std::vector<Vertex> generateIcosahedron() {
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;

    glm::vec3 rawPositions[12] = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };

    glm::vec3 positions[12];
    for (int i = 0; i < 12; ++i) {
        positions[i] = glm::normalize(rawPositions[i]);
    }

    int faces[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    std::vector<Vertex> vertices;
    vertices.reserve(60);

    for (int f = 0; f < 20; ++f) {
        glm::vec3 p0 = positions[faces[f][0]];
        glm::vec3 p1 = positions[faces[f][1]];
        glm::vec3 p2 = positions[faces[f][2]];
        glm::vec3 norm = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        glm::vec3 tri[3] = {p0, p1, p2};
        for (int v = 0; v < 3; ++v) {
            // Сферические UV
            float u = 0.5f + atan2(tri[v].z, tri[v].x) / (2.0f * 3.14159265f);
            float vv = 0.5f - asin(tri[v].y) / 3.14159265f;
            vertices.push_back({ tri[v], norm, glm::vec2(u, vv) });
        }
    }

    return vertices;
}

void render(const graphics::internal::FrameData& fd) {
    if (graphics::internal::context.swapchain_extent.width == 0 ||
        graphics::internal::context.swapchain_extent.height == 0) {
        return;
    }

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

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

    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer, &offset);

    // Рисуем 3 объекта с динамическим offset
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        uint32_t dynamicOffset = static_cast<uint32_t>(i * dynamicAlignment);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &descriptorSet, 1, &dynamicOffset);
        vkCmdDraw(fd.command_buffer, vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application