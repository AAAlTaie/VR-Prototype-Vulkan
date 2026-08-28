#include "render/SplatPass.h"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

#include "platform/Paths.h"

namespace render {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
constexpr size_t kProjectedEntrySize = 48;
constexpr uint32_t kHistogramBuckets = 65536;

void globalBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 sourceStage,
                   VkAccessFlags2 sourceAccess, VkPipelineStageFlags2 destinationStage,
                   VkAccessFlags2 destinationAccess) {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void computeToCompute(VkCommandBuffer commandBuffer) {
    globalBarrier(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

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

core::Result<UniquePipelineLayout> createLayout(VkDevice device, VkShaderStageFlags stages,
                                                uint32_t size) {
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
    return UniquePipelineLayout(device, layout);
}

core::Result<UniquePipeline> buildComputePipeline(VkDevice device, VkPipelineLayout layout,
                                                  const std::filesystem::path& shader) {
    auto module = createModule(device, shader);
    if (!module) {
        return core::Error{module.error()};
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module.value();
    info.stage.pName = "main";
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult created = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module.value(), nullptr);

    if (created != VK_SUCCESS) {
        return core::Error{"vkCreateComputePipelines failed for " + shader.string()};
    }
    return UniquePipeline(device, pipeline);
}

}

core::Result<SplatPass> SplatPass::create(const VulkanContext& context, VkFormat colourFormat,
                                          uint32_t splatCount, uint32_t viewCount) {
    SplatPass pass;
    pass.device_ = context.device();
    pass.viewCount_ = viewCount;

    struct StageSpec {
        Stage* target;
        uint32_t constantSize;
        const char* shader;
    };

    const StageSpec specs[]{
        {&pass.projection_, sizeof(ProjectionConstants), "shaders/project.comp.spv"},
        {&pass.clear_, sizeof(ClearConstants), "shaders/sort_clear.comp.spv"},
        {&pass.histogram_, sizeof(HistogramConstants), "shaders/sort_histogram.comp.spv"},
        {&pass.scan_, sizeof(ScanConstants), "shaders/sort_scan.comp.spv"},
        {&pass.scatter_, sizeof(ScatterConstants), "shaders/sort_scatter.comp.spv"},
        {&pass.combine_, sizeof(CombineConstants), "shaders/sort_combine.comp.spv"},
    };

    for (const StageSpec& spec : specs) {
        auto layout = createLayout(pass.device_, VK_SHADER_STAGE_COMPUTE_BIT, spec.constantSize);
        if (!layout) {
            return core::Error{layout.error()};
        }
        spec.target->layout = layout.take();

        auto pipeline = buildComputePipeline(pass.device_, spec.target->layout.get(), spec.shader);
        if (!pipeline) {
            return core::Error{pipeline.error()};
        }
        spec.target->pipeline = pipeline.take();
    }

    auto rasterLayout = createLayout(pass.device_, VK_SHADER_STAGE_VERTEX_BIT, sizeof(RasterConstants));
    if (!rasterLayout) {
        return core::Error{rasterLayout.error()};
    }
    pass.raster_.layout = rasterLayout.take();

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
    rendering.viewMask = (1u << viewCount) - 1u;
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
    pipelineInfo.layout = pass.raster_.layout.get();

    VkPipeline rasterPipeline = VK_NULL_HANDLE;
    const VkResult created = vkCreateGraphicsPipelines(pass.device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                       nullptr, &rasterPipeline);
    vkDestroyShaderModule(pass.device_, fragment.value(), nullptr);
    vkDestroyShaderModule(pass.device_, vertex.value(), nullptr);

    if (created != VK_SUCCESS) {
        return core::Error{"vkCreateGraphicsPipelines failed"};
    }
    pass.raster_.pipeline = UniquePipeline(pass.device_, rasterPipeline);

    const VkBufferUsageFlags storageUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(splatCount) * sizeof(uint32_t);

    for (uint32_t index = 0; index < viewCount; ++index) {
        EyeResources& eye = pass.eyes_[index];

        auto projected = GpuBuffer::createDeviceLocal(
            context.allocator(), static_cast<VkDeviceSize>(splatCount) * kProjectedEntrySize,
            storageUsage);
        if (!projected) {
            return core::Error{projected.error()};
        }

        auto keys = GpuBuffer::createDeviceLocal(context.allocator(), indexBytes, storageUsage);
        if (!keys) {
            return core::Error{keys.error()};
        }

        auto sorted = GpuBuffer::createDeviceLocal(context.allocator(), indexBytes, storageUsage);
        if (!sorted) {
            return core::Error{sorted.error()};
        }

        auto arguments = GpuBuffer::createDeviceLocal(
            context.allocator(), sizeof(VkDrawIndirectCommand),
            storageUsage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (!arguments) {
            return core::Error{arguments.error()};
        }

        eye.projected = projected.take();
        eye.keys = keys.take();
        eye.sorted = sorted.take();
        eye.drawArguments = arguments.take();
        eye.projectedAddress = eye.projected.deviceAddress(pass.device_);
        eye.keysAddress = eye.keys.deviceAddress(pass.device_);
        eye.sortedAddress = eye.sorted.deviceAddress(pass.device_);
        eye.drawAddress = eye.drawArguments.deviceAddress(pass.device_);
    }

    auto combined = GpuBuffer::createDeviceLocal(
        context.allocator(), sizeof(VkDrawIndirectCommand),
        storageUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!combined) {
        return core::Error{combined.error()};
    }

    auto histogram = GpuBuffer::createDeviceLocal(
        context.allocator(), static_cast<VkDeviceSize>(kHistogramBuckets) * sizeof(uint32_t),
        storageUsage);
    if (!histogram) {
        return core::Error{histogram.error()};
    }

    auto dispatchArguments = GpuBuffer::createDeviceLocal(
        context.allocator(), sizeof(VkDispatchIndirectCommand),
        storageUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    if (!dispatchArguments) {
        return core::Error{dispatchArguments.error()};
    }

    auto statistics = GpuBuffer::createReadback(context.allocator(), sizeof(VkDrawIndirectCommand));
    if (!statistics) {
        return core::Error{statistics.error()};
    }

    pass.combinedArguments_ = combined.take();
    pass.histogramBuffer_ = histogram.take();
    pass.dispatchArguments_ = dispatchArguments.take();
    pass.statistics_ = statistics.take();
    pass.combinedAddress_ = pass.combinedArguments_.deviceAddress(pass.device_);
    pass.histogramAddress_ = pass.histogramBuffer_.deviceAddress(pass.device_);
    pass.dispatchAddress_ = pass.dispatchArguments_.deviceAddress(pass.device_);

    return core::Result<SplatPass>(std::move(pass));
}

void SplatPass::recordProjection(VkCommandBuffer commandBuffer, const EyeResources& eye,
                                 const glm::mat4& view, glm::vec2 focal, glm::vec2 viewport,
                                 VkDeviceAddress splats, uint32_t splatCount, float depthMinimum,
                                 float depthMaximum) const {
    const VkDrawIndirectCommand reset{4, 0, 0, 0};
    vkCmdUpdateBuffer(commandBuffer, eye.drawArguments.handle(), 0, sizeof(reset), &reset);

    globalBarrier(commandBuffer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    ProjectionConstants constants{};
    constants.view = view;
    constants.focal = focal;
    constants.viewport = viewport;
    constants.splats = splats;
    constants.projected = eye.projectedAddress;
    constants.draw = eye.drawAddress;
    constants.keys = eye.keysAddress;
    constants.splatCount = splatCount;
    constants.depthMinimum = depthMinimum;
    constants.depthMaximum = depthMaximum;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, projection_.pipeline.get());
    vkCmdPushConstants(commandBuffer, projection_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, (splatCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);

    computeToCompute(commandBuffer);
}

void SplatPass::recordSort(VkCommandBuffer commandBuffer, const EyeResources& eye) const {
    ClearConstants clear{};
    clear.histogram = histogramAddress_;
    clear.draw = eye.drawAddress;
    clear.dispatch = dispatchAddress_;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clear_.pipeline.get());
    vkCmdPushConstants(commandBuffer, clear_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(clear),
                       &clear);
    vkCmdDispatch(commandBuffer, kHistogramBuckets / kWorkgroupSize, 1, 1);

    computeToCompute(commandBuffer);

    HistogramConstants histogram{};
    histogram.keys = eye.keysAddress;
    histogram.histogram = histogramAddress_;
    histogram.draw = eye.drawAddress;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, histogram_.pipeline.get());
    vkCmdPushConstants(commandBuffer, histogram_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(histogram), &histogram);
    vkCmdDispatchIndirect(commandBuffer, dispatchArguments_.handle(), 0);

    computeToCompute(commandBuffer);

    ScanConstants scan{};
    scan.histogram = histogramAddress_;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, scan_.pipeline.get());
    vkCmdPushConstants(commandBuffer, scan_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(scan),
                       &scan);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    computeToCompute(commandBuffer);

    ScatterConstants scatter{};
    scatter.keys = eye.keysAddress;
    scatter.histogram = histogramAddress_;
    scatter.sorted = eye.sortedAddress;
    scatter.draw = eye.drawAddress;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, scatter_.pipeline.get());
    vkCmdPushConstants(commandBuffer, scatter_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(scatter), &scatter);
    vkCmdDispatchIndirect(commandBuffer, dispatchArguments_.handle(), 0);

    computeToCompute(commandBuffer);
}

void SplatPass::recordEye(VkCommandBuffer commandBuffer, uint32_t eye, const glm::mat4& view,
                          glm::vec2 focal, glm::vec2 viewport, VkDeviceAddress splats,
                          uint32_t splatCount, float depthMinimum, float depthMaximum) const {
    const EyeResources& resources = eyes_[eye];
    recordProjection(commandBuffer, resources, view, focal, viewport, splats, splatCount, depthMinimum,
                     depthMaximum);
    recordSort(commandBuffer, resources);
}

void SplatPass::recordCombine(VkCommandBuffer commandBuffer) const {
    CombineConstants constants{};
    constants.left = eyes_[0].drawAddress;
    constants.right = eyes_[viewCount_ > 1 ? 1 : 0].drawAddress;
    constants.combined = combinedAddress_;
    constants.viewCount = viewCount_;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, combine_.pipeline.get());
    vkCmdPushConstants(commandBuffer, combine_.layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    globalBarrier(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                      VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferCopy region{};
    region.size = sizeof(VkDrawIndirectCommand);
    vkCmdCopyBuffer(commandBuffer, combinedArguments_.handle(), statistics_.handle(), 1, &region);
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
    for (uint32_t index = 0; index < kMaxViews; ++index) {
        const EyeResources& eye = eyes_[index < viewCount_ ? index : 0];
        constants.projected[index] = eye.projectedAddress;
        constants.sorted[index] = eye.sortedAddress;
        constants.counts[index] = eye.drawAddress;
    }

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, raster_.pipeline.get());
    vkCmdPushConstants(commandBuffer, raster_.layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDrawIndirect(commandBuffer, combinedArguments_.handle(), 0, 1, sizeof(VkDrawIndirectCommand));
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

}
