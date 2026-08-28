#include "render/SplatPass.h"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

#include "platform/Paths.h"

namespace render {
namespace {

constexpr uint32_t kProjectionWorkgroupSize = 256;
constexpr size_t kProjectedEntrySize = 48;

core::Result<VkShaderModule> createModule(VkDevice device, const std::filesystem::path& relative) {
    auto resolved = platform::resolveResource(relative);
    if (!resolved) {
        return core::Error{resolved.error()};
    }

    std::ifstream stream(resolved.value(), std::ios::binary | std::ios::ate);
    if (!stream) {
        return core::Error{"cannot open shader " + resolved.value().string()};
    }

    const auto size = static_cast<size_t>(stream.tellg());
    if (size == 0 || size % sizeof(uint32_t) != 0) {
        return core::Error{"malformed SPIR-V in " + resolved.value().string()};
    }

    std::vector<uint32_t> code(size / sizeof(uint32_t));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return core::Error{"vkCreateShaderModule failed for " + relative.string()};
    }
    return module;
}

core::Result<VkPipelineLayout> createLayout(VkDevice device, VkShaderStageFlags stages, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = stages;
    range.size = size;

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        return core::Error{"vkCreatePipelineLayout failed"};
    }
    return layout;
}

}

core::Result<SplatPass> SplatPass::create(const VulkanContext& context, VkFormat colourFormat,
                                          uint32_t splatCount) {
    SplatPass pass;
    pass.device_ = context.device();

    auto projectionLayout = createLayout(pass.device_, VK_SHADER_STAGE_COMPUTE_BIT,
                                         sizeof(ProjectionConstants));
    if (!projectionLayout) {
        return core::Error{projectionLayout.error()};
    }
    pass.projectionLayout_ = projectionLayout.value();

    auto rasterLayout = createLayout(pass.device_, VK_SHADER_STAGE_VERTEX_BIT, sizeof(RasterConstants));
    if (!rasterLayout) {
        return core::Error{rasterLayout.error()};
    }
    pass.rasterLayout_ = rasterLayout.value();

    auto compute = createModule(pass.device_, "shaders/project.comp.spv");
    if (!compute) {
        return core::Error{compute.error()};
    }

    VkComputePipelineCreateInfo computeInfo{};
    computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeInfo.stage.module = compute.value();
    computeInfo.stage.pName = "main";
    computeInfo.layout = pass.projectionLayout_;

    const VkResult computeCreated = vkCreateComputePipelines(pass.device_, VK_NULL_HANDLE, 1, &computeInfo,
                                                             nullptr, &pass.projectionPipeline_);
    vkDestroyShaderModule(pass.device_, compute.value(), nullptr);
    if (computeCreated != VK_SUCCESS) {
        return core::Error{"vkCreateComputePipelines failed"};
    }

    auto vertex = createModule(pass.device_, "shaders/splat.vert.spv");
    if (!vertex) {
        return core::Error{vertex.error()};
    }
    auto fragment = createModule(pass.device_, "shaders/splat.frag.spv");
    if (!fragment) {
        vkDestroyShaderModule(pass.device_, vertex.value(), nullptr);
        return core::Error{fragment.error()};
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.value();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment.value();
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colourFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pass.rasterLayout_;

    const VkResult rasterCreated = vkCreateGraphicsPipelines(pass.device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                             nullptr, &pass.rasterPipeline_);
    vkDestroyShaderModule(pass.device_, fragment.value(), nullptr);
    vkDestroyShaderModule(pass.device_, vertex.value(), nullptr);
    if (rasterCreated != VK_SUCCESS) {
        return core::Error{"vkCreateGraphicsPipelines failed"};
    }

    auto projected = GpuBuffer::createDeviceLocal(
        context.allocator(), static_cast<VkDeviceSize>(splatCount) * kProjectedEntrySize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    if (!projected) {
        return core::Error{projected.error()};
    }

    auto arguments = GpuBuffer::createDeviceLocal(
        context.allocator(), sizeof(VkDrawIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!arguments) {
        return core::Error{arguments.error()};
    }

    auto statistics = GpuBuffer::createReadback(context.allocator(), sizeof(VkDrawIndirectCommand));
    if (!statistics) {
        return core::Error{statistics.error()};
    }

    pass.statistics_ = statistics.take();
    pass.projected_ = projected.take();
    pass.drawArguments_ = arguments.take();
    pass.projectedAddress_ = pass.projected_.deviceAddress(pass.device_);
    pass.drawAddress_ = pass.drawArguments_.deviceAddress(pass.device_);

    return core::Result<SplatPass>(std::move(pass));
}

void SplatPass::recordProjection(VkCommandBuffer commandBuffer, const glm::mat4& view, glm::vec2 focal,
                                 glm::vec2 viewport, VkDeviceAddress splats, uint32_t splatCount,
                                 float nearPlane) const {
    const VkDrawIndirectCommand reset{4, 0, 0, 0};
    vkCmdUpdateBuffer(commandBuffer, drawArguments_.handle(), 0, sizeof(reset), &reset);

    VkMemoryBarrier2 toCompute{};
    toCompute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    toCompute.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toCompute.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toCompute.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    toCompute.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

    VkDependencyInfo firstDependency{};
    firstDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    firstDependency.memoryBarrierCount = 1;
    firstDependency.pMemoryBarriers = &toCompute;
    vkCmdPipelineBarrier2(commandBuffer, &firstDependency);

    ProjectionConstants constants{};
    constants.view = view;
    constants.focal = focal;
    constants.viewport = viewport;
    constants.splats = splats;
    constants.projected = projectedAddress_;
    constants.draw = drawAddress_;
    constants.splatCount = splatCount;
    constants.nearPlane = nearPlane;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, projectionPipeline_);
    vkCmdPushConstants(commandBuffer, projectionLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, (splatCount + kProjectionWorkgroupSize - 1) / kProjectionWorkgroupSize, 1,
                  1);

    VkMemoryBarrier2 toDraw{};
    toDraw.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    toDraw.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    toDraw.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    toDraw.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_COPY_BIT;
    toDraw.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                           VK_ACCESS_2_TRANSFER_READ_BIT;

    VkDependencyInfo secondDependency{};
    secondDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    secondDependency.memoryBarrierCount = 1;
    secondDependency.pMemoryBarriers = &toDraw;
    vkCmdPipelineBarrier2(commandBuffer, &secondDependency);

    VkBufferCopy region{};
    region.size = sizeof(VkDrawIndirectCommand);
    vkCmdCopyBuffer(commandBuffer, drawArguments_.handle(), statistics_.handle(), 1, &region);
}

uint32_t SplatPass::visibleSplats() const {
    const void* mapped = statistics_.mappedData();
    if (mapped == nullptr) {
        return 0;
    }
    VkDrawIndirectCommand command{};
    std::memcpy(&command, mapped, sizeof(command));
    return command.instanceCount;
}

void SplatPass::recordRaster(VkCommandBuffer commandBuffer, VkExtent2D extent) const {
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = extent;

    RasterConstants constants{};
    constants.viewport = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    constants.projected = projectedAddress_;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline_);
    vkCmdPushConstants(commandBuffer, rasterLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDrawIndirect(commandBuffer, drawArguments_.handle(), 0, 1, sizeof(VkDrawIndirectCommand));
}

SplatPass::SplatPass(SplatPass&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      projectionLayout_(std::exchange(other.projectionLayout_, VK_NULL_HANDLE)),
      projectionPipeline_(std::exchange(other.projectionPipeline_, VK_NULL_HANDLE)),
      rasterLayout_(std::exchange(other.rasterLayout_, VK_NULL_HANDLE)),
      rasterPipeline_(std::exchange(other.rasterPipeline_, VK_NULL_HANDLE)),
      projected_(std::move(other.projected_)),
      drawArguments_(std::move(other.drawArguments_)),
      statistics_(std::move(other.statistics_)),
      projectedAddress_(std::exchange(other.projectedAddress_, 0)),
      drawAddress_(std::exchange(other.drawAddress_, 0)) {}

SplatPass& SplatPass::operator=(SplatPass&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        projectionLayout_ = std::exchange(other.projectionLayout_, VK_NULL_HANDLE);
        projectionPipeline_ = std::exchange(other.projectionPipeline_, VK_NULL_HANDLE);
        rasterLayout_ = std::exchange(other.rasterLayout_, VK_NULL_HANDLE);
        rasterPipeline_ = std::exchange(other.rasterPipeline_, VK_NULL_HANDLE);
        projected_ = std::move(other.projected_);
        drawArguments_ = std::move(other.drawArguments_);
        statistics_ = std::move(other.statistics_);
        projectedAddress_ = std::exchange(other.projectedAddress_, 0);
        drawAddress_ = std::exchange(other.drawAddress_, 0);
    }
    return *this;
}

SplatPass::~SplatPass() {
    destroy();
}

void SplatPass::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (rasterPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, rasterPipeline_, nullptr);
    }
    if (rasterLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, rasterLayout_, nullptr);
    }
    if (projectionPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, projectionPipeline_, nullptr);
    }
    if (projectionLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, projectionLayout_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
}

}
