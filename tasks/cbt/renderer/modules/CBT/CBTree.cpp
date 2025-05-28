#include "CBTree.hpp"

#include <etna/PipelineManager.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>
#include <etna/Assert.hpp>


std::int32_t CBTree::heapByteSize(std::int32_t max_depth)
{
  return 1 << (max_depth - 1);
}

CBTree::Node CBTree::getDeepestLeaf(Node node)
{
  Node leaf = {
    .index = static_cast<uint32_t>(node.index << (maxDepth - node.depth)),
    .depth = maxDepth,
  };
  return (node.index == 0) ? node : leaf;
}

void CBTree::setBit(std::uint32_t* bit_field, std::uint32_t bit_index, std::uint32_t bit_value)
{
  std::uint32_t bitMask = ~(1u << bit_index);

  (*bit_field) &= bitMask;
  (*bit_field) |= (bit_value << bit_index);
}

void CBTree::heapWriteBitfield(std::uint32_t* heap, CBTree::Node node, std::uint32_t bit_value)
{
  Node leaf = getDeepestLeaf(node);

  // getBitIndexLeaf
  std::int32_t level = 2 << leaf.depth;
  std::int32_t bitsAmount = maxDepth - leaf.depth + 1;
  std::int32_t bitIndex = level + leaf.index * bitsAmount;

  // SetBit
  setBit(&heap[bitIndex >> 5], bitIndex & 31, bit_value);
}

CBTree::CBTree(std::int32_t max_depth)
  : maxDepth(max_depth)
{
  ETNA_VERIFYF(max_depth >= 5, "Minimum depth is 5");
  ETNA_VERIFYF(max_depth <= 29, "Maximum depth is 29");
}

void CBTree::allocateResources()
{

  auto& ctx = etna::get_context();

  cbtBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = static_cast<vk::DeviceSize>(heapByteSize(maxDepth)),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "cbtBuffer"});

  cbtDrawIndirectBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(vk::DrawIndirectCommand),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "cbtDrawIndirectBuffer"});

  oneShotCommands = ctx.createOneShotCmdMgr();
}

void CBTree::loadShaders()
{
  etna::create_program(
    "reduction_prepass", {CBT_MODULE_SHADERS_ROOT "cbt_reduction_prepass.comp.spv"});
  etna::create_program("reduction_step", {CBT_MODULE_SHADERS_ROOT "cbt_sum_reduction.comp.spv"});
  etna::create_program(
    "prepare_indirect", {CBT_MODULE_SHADERS_ROOT "cbt_prepare_indirect.comp.spv"});
}

void CBTree::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  cbtReductionPrepassPipeline = pipelineManager.createComputePipeline("reduction_prepass", {});
  cbtReductionPipeline = pipelineManager.createComputePipeline("reduction_step", {});
  cbtPrepareIndirectPipeline = pipelineManager.createComputePipeline("prepare_indirect", {});
}

void CBTree::load()
{
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = static_cast<vk::DeviceSize>(heapByteSize(maxDepth))});

  {
    std::uint32_t* data = reinterpret_cast<std::uint32_t*>(
      std::calloc(heapByteSize(maxDepth) >> 2, sizeof(std::uint32_t)));

    data[0] = 1u << maxDepth; // max_depth = findLSB(heap[0]);

    std::uint32_t firstTriangle = 2u;
    std::uint32_t secondTriangle = 3u;

    heapWriteBitfield(data, {.index = firstTriangle, .depth = 1}, 1u);
    heapWriteBitfield(data, {.index = secondTriangle, .depth = 1}, 1u);

    transferHelper->uploadBuffer(
      *oneShotCommands, cbtBuffer, 0, std::as_bytes(std::span(data, heapByteSize(maxDepth) >> 2)));

    std::free(data);
  }

  std::vector<vk::DrawIndirectCommand> command = {
    {.vertexCount = 2, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};

  transferHelper->uploadBuffer(
    *oneShotCommands, cbtDrawIndirectBuffer, 0, std::as_bytes(std::span(command)));


  auto commandBuffer = oneShotCommands->start();
  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    reduct(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());
  oneShotCommands->submitAndWait(commandBuffer);
}

void CBTree::prepareIndirect(vk::CommandBuffer cmd_buf)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("prepare_indirect");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0 /*check cbt.glsl*/),
    cmd_buf,
    {etna::Binding{0 /*check cbt.glsl*/, cbtBuffer.genBinding()},
     etna::Binding{1, cbtDrawIndirectBuffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cbtPrepareIndirectPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute,
    cbtPrepareIndirectPipeline.getVkPipelineLayout(),
    0,
    {vkSet},
    {});

  cmd_buf.dispatch(1, 1, 1);
}

void CBTree::reduct(vk::CommandBuffer cmd_buf)
{
  ZoneScoped;

  {
    ETNA_PROFILE_GPU(cmd_buf, reductionPrepass)
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, cbtReductionPrepassPipeline.getVkPipeline());
    reductionPrepass(cmd_buf, cbtReductionPrepassPipeline.getVkPipelineLayout());
  }

  {
    std::array bufferBarriers = {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
      .buffer = cbtBuffer.get(),
      .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  for (std::int32_t i = (maxDepth - 5) - 1; i >= 0; i--)
  {
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cbtReductionPipeline.getVkPipeline());
    reductionStep(cmd_buf, cbtReductionPipeline.getVkPipelineLayout(), i);

    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = cbtBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      cmd_buf.pipelineBarrier2(dependencyInfo);
    }
  }
}

void CBTree::reductionPrepass(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  std::int32_t amount = ((1 << maxDepth) >> 5);
  std::int32_t groupCount = (amount >= 256) ? (amount >> 8) : 1;

  auto shaderInfo = etna::get_shader_program("reduction_prepass");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0 /*check cbt.glsl*/),
    cmd_buf,
    {etna::Binding{0 /*check cbt.glsl*/, cbtBuffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, {vkSet}, {});

  cmd_buf.pushConstants<std::int32_t>(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {maxDepth});

  cmd_buf.dispatch(static_cast<uint32_t>(groupCount), 1, 1);
}

void CBTree::reductionStep(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, std::int32_t depth)
{
  ZoneScoped;

  std::int32_t amount = 1 << depth;
  std::int32_t groupCount = (amount >= 256) ? (amount >> 8) : 1;

  auto shaderInfo = etna::get_shader_program("reduction_step");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0 /*check cbt.glsl*/),
    cmd_buf,
    {etna::Binding{0 /*check cbt.glsl*/, cbtBuffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, {vkSet}, {});

  cmd_buf.pushConstants<std::int32_t>(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {depth});

  cmd_buf.dispatch(static_cast<uint32_t>(groupCount), 1, 1);
}
