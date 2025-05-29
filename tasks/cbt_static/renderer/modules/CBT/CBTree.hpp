#pragma once

#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>


class CBTree
{
public:
  explicit CBTree(std::int32_t max_depth);

  void allocateResources();
  void loadShaders();
  void setupPipelines();
  void load();

  void prepareIndirect(vk::CommandBuffer cmd_buf);
  void reduct(vk::CommandBuffer cmd_buf);

  const etna::Buffer& getCBTBuffer() const { return cbtBuffer; }
  const etna::Buffer& getDrawIndirectBuffer() const { return cbtDrawIndirectBuffer; }
  std::int32_t getMaxDepth() const { return maxDepth; }

private:
  struct Node
  {
    std::uint32_t index;
    std::int32_t depth;
  };

private:
  std::int32_t heapByteSize(std::int32_t max_depth);
  Node getDeepestLeaf(Node node);
  void setBit(std::uint32_t* bit_field, std::uint32_t bit_index, std::uint32_t bit_value);
  void heapWriteBitfield(std::uint32_t* heap, Node node, std::uint32_t bit_value);

  void reductionPrepass(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void reductionStep(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, std::int32_t depth);

private:
  std::int32_t maxDepth;

  etna::Buffer cbtBuffer;
  etna::Buffer cbtDrawIndirectBuffer;

  etna::ComputePipeline cbtReductionPrepassPipeline;
  etna::ComputePipeline cbtReductionPipeline;
  etna::ComputePipeline cbtPrepareIndirectPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
