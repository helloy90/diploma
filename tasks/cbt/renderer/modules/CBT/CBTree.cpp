#include "CBTree.hpp"

#include <etna/Assert.hpp>


std::uint64_t CBTree::heapByteSize(std::uint64_t max_depth)
{
  return std::uint64_t(1) << (max_depth - 1);
}


CBTree::CBTree(std::uint64_t max_depth)
  : heap(heapByteSize(max_depth) / sizeof(std::uint64_t))
{
  ETNA_VERIFYF(max_depth >= 5, "Minimum depth is 5");
  ETNA_VERIFYF(max_depth <= 58, "Maximum depth is 58");
}
