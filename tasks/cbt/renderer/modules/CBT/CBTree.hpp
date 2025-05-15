#pragma once

#include <cstdint>
#include <vector>


class CBTree
{
public:
  struct Node
  {
    std::uint64_t heap_index;
    std::uint64_t depth;
  };

public:
  static std::uint64_t heapByteSize(std::uint64_t max_depth);

public:
  explicit CBTree(std::uint64_t max_depth);

private:
  std::vector<std::uint64_t> heap;
};
