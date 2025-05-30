#ifndef CBT_GLSL_INCLUDED
#define CBT_GLSL_INCLUDED


// only once cbt for now
layout(std430, set = 0, binding = 0) buffer cbt_buffer
{
  uint heap[];
}
cbt;

struct CBTNode
{
  uint index; // heap index
  int depth;  // aka most significant bit in heap index
};

struct _CBTHeapQueryArgs
{
  uint heapIndexLeft;
  uint heapIndexRight;
  uint bitOffsetLeft;
  uint bitCountLeft;
  uint bitCountRight;
};

// Main Operations (O(1))
void cbtNodeSplitFast(CBTNode node);
void cbtNodeSplit(CBTNode node);
void cbtNodeMergeFast(CBTNode node);
void cbtNodeMerge(CBTNode node);

// Node queries (O(1))
bool cbtNodeIsDeepestLeaf(CBTNode node);
bool cbtNodeIsLeaf(CBTNode node);
bool cbtNodeIsRoot(CBTNode node);
bool cbtNodeIsNull(CBTNode node);

// Tree queries (O(1))
int cbtMaxDepth();
uint cbtNodeCount();
uint cbtHeapRead(CBTNode node);

// Node constructors (O(1))
CBTNode cbtNodeGet(uint heap_index, int depth);
CBTNode cbtNodeGet(uint heap_index);
CBTNode cbtNodeGetParentFast(CBTNode node);
CBTNode cbtNodeGetParent(CBTNode node);
CBTNode cbtNodeGetSiblingFast(CBTNode node);
CBTNode cbtNodeGetSibling(CBTNode node);
CBTNode cbtNodeGetLeftSiblingFast(CBTNode node);
CBTNode cbtNodeGetLeftSibling(CBTNode node);
CBTNode cbtNodeGetRightSiblingFast(CBTNode node);
CBTNode cbtNodeGetRightSibling(CBTNode node);
CBTNode cbtNodeGetLeftChildFast(CBTNode node);
CBTNode cbtNodeGetLeftChild(CBTNode node);
CBTNode cbtNodeGetRightChildFast(CBTNode node);
CBTNode cbtNodeGetRightChild(CBTNode node);

// Node Encoding (O(depth))
uint cbtNodeEncode(CBTNode node);
CBTNode cbtNodeDecode(uint node_code);


const uint kFullBitField = 0xFFFFFFFFu;

// ----- 32 bit field data manipulation -----
uint _cbtGetBit(uint bit_field, uint bit_index)
{
  return ((bit_field >> bit_index) & 1u);
}

void _cbtSetBit(uint storage_index, uint bit_index, uint bit_value)
{
  const uint bitMask = ~(1u << bit_index);

  atomicAnd(cbt.heap[storage_index], bitMask);
  atomicOr(cbt.heap[storage_index], bit_value << bit_index);
}

uint _cbtGetBitRange(uint bit_field, uint first_bit_index, uint bit_count)
{
  const uint bitMask = ~(kFullBitField << bit_count);
  return (bit_field >> first_bit_index) & bitMask;
}

void _cbtSetBitRange(uint storage_index, uint first_bit_index, uint bit_count, uint bit_data)
{
  const uint bitMask = ~(~(kFullBitField << bit_count) << first_bit_index);

  atomicAnd(cbt.heap[storage_index], bitMask);
  atomicOr(cbt.heap[storage_index], bit_data << first_bit_index);
}
// ------------------------------------------

// --- Tree queries ---
int cbtMaxDepth()
{
  return findLSB(cbt.heap[0]);
}

uint cbtNodeCount()
{
  return cbtHeapRead(cbtNodeGet(1u, 0));
}

uint _cbtHeapSizeBytes()
{
  return 1u << (cbtMaxDepth() - 1);
}

uint _cbtHeapSizeU32s()
{
  return _cbtHeapSizeBytes() >> 2;
}
// -------------------

// --- Tree node queries ---
CBTNode cbtNodeGet(uint heap_index, int depth)
{
  return CBTNode(heap_index, depth);
}

CBTNode cbtNodeGet(uint heap_index)
{
  return cbtNodeGet(heap_index, findMSB(heap_index));
}

bool cbtNodeIsDeepestLeaf(CBTNode node)
{
  return (node.depth == cbtMaxDepth());
}

bool cbtNodeIsLeaf(CBTNode node)
{
  return (cbtHeapRead(node) == 1u);
}

bool cbtNodeIsRoot(CBTNode node)
{
  return (node.index == 1u);
}

bool cbtNodeIsNull(CBTNode node)
{
  return (node.index == 0u);
}

CBTNode cbtNodeGetParentFast(CBTNode node)
{
  return cbtNodeGet(node.index >> 1, node.depth - 1);
}
CBTNode cbtNodeGetParent(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetParentFast(node);
}

CBTNode _cbtNodeGetDeepestLeafFast(CBTNode node)
{
  int maxDepth = cbtMaxDepth();
  return cbtNodeGet(node.index << (maxDepth - node.depth), maxDepth);
}
CBTNode _cbtNodeGetDeepestLeaf(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : _cbtNodeGetDeepestLeafFast(node);
}

CBTNode cbtNodeGetSiblingFast(CBTNode node)
{
  return cbtNodeGet(node.index ^ 1u, node.depth);
}
CBTNode cbtNodeGetSibling(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetSiblingFast(node);
}

CBTNode cbtNodeGetLeftSiblingFast(CBTNode node)
{
  return cbtNodeGet(node.index & (~1u), node.depth);
}
CBTNode cbtNodeGetLeftSibling(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetLeftSiblingFast(node);
}

CBTNode cbtNodeGetRightSiblingFast(CBTNode node)
{
  return cbtNodeGet(node.index | 1u, node.depth);
}
CBTNode cbtNodeGetRightSibling(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetRightSiblingFast(node);
}

CBTNode cbtNodeGetLeftChildFast(CBTNode node)
{
  return cbtNodeGet(node.index << 1, node.depth + 1);
}
CBTNode cbtNodeGetLeftChild(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetLeftChildFast(node);
}

CBTNode cbtNodeGetRightChildFast(CBTNode node)
{
  return cbtNodeGet((node.index << 1) | 1u, node.depth + 1);
}
CBTNode cbtNodeGetRightChild(CBTNode node)
{
  return cbtNodeIsNull(node) ? node : cbtNodeGetRightChildFast(node);
}

uint _cbtNodeBitIndex(CBTNode node)
{
  uint bitsAmount = uint(cbtMaxDepth() + 1 - node.depth);
  uint level = 2u << node.depth;
  return level + node.index * bitsAmount;
}

uint _cbtNodeBitIndexDeepestLeaf(CBTNode node)
{
  return _cbtNodeBitIndex(_cbtNodeGetDeepestLeaf(node));
}

int _cbtNodeBitSize(CBTNode node)
{
  return cbtMaxDepth() - node.depth + 1;
}
// --------------------------

// --- Heap manipulations ---

// needed when bit range is overlapping two uint storages
_CBTHeapQueryArgs _cbtGetHeapQueryArgs(CBTNode node, int bit_count)
{
  uint nodeIndexLSB = _cbtNodeBitIndex(node);
  uint maxHeapIndex = _cbtHeapSizeU32s() - 1u;
  uint heapIndexLeft = (nodeIndexLSB >> 5u);
  uint heapIndexRight = min(heapIndexLeft + 1, maxHeapIndex);

  uint bitOffsetLeft = nodeIndexLSB & 31u;
  uint bitCountLeft = min(32u - bitOffsetLeft, bit_count);
  uint bitCountRight = bit_count - bitCountLeft;

  return _CBTHeapQueryArgs(
    heapIndexLeft, heapIndexRight, bitOffsetLeft, bitCountLeft, bitCountRight);
}

uint _cbtHeapReadBitfield(CBTNode node)
{
  uint bit_index = _cbtNodeBitIndexDeepestLeaf(node);
  return _cbtGetBit(cbt.heap[bit_index >> 5u], bit_index & 31u);
}

void _cbtHeapWriteBitfield(CBTNode node, uint bit_value)
{
  uint bit_index = _cbtNodeBitIndexDeepestLeaf(node);
  _cbtSetBit(bit_index >> 5u, bit_index & 31u, bit_value);
}

uint _cbtHeapReadDirect(CBTNode node, int bit_count)
{
  _CBTHeapQueryArgs args = _cbtGetHeapQueryArgs(node, bit_count);

  uint leftSideBits =
    _cbtGetBitRange(cbt.heap[args.heapIndexLeft], args.bitOffsetLeft, args.bitCountLeft);
  uint rightSideBits = _cbtGetBitRange(cbt.heap[args.heapIndexRight], 0u, args.bitCountRight);

  return (leftSideBits | (rightSideBits << args.bitCountLeft));
}
uint cbtHeapRead(CBTNode node)
{
  return _cbtHeapReadDirect(node, _cbtNodeBitSize(node));
}

void _cbtHeapWriteDirect(CBTNode node, int bit_count, uint bit_data)
{
  _CBTHeapQueryArgs args = _cbtGetHeapQueryArgs(node, bit_count);

  _cbtSetBitRange(args.heapIndexLeft, args.bitOffsetLeft, args.bitCountLeft, bit_data);
  _cbtSetBitRange(args.heapIndexRight, 0u, args.bitCountRight, bit_data >> args.bitCountLeft);
}
void _cbtHeapWrite(CBTNode node, uint bit_data)
{
  _cbtHeapWriteDirect(node, _cbtNodeBitSize(node), bit_data);
}
// --------------------------

// --- Main Operations ---
void cbtNodeSplitFast(CBTNode node)
{
  _cbtHeapWriteBitfield(cbtNodeGetRightChild(node), 1u);
}
void cbtNodeSplit(CBTNode node)
{
  if (!cbtNodeIsDeepestLeaf(node))
  {
    cbtNodeSplitFast(node);
  }
}

void cbtNodeMergeFast(CBTNode node)
{
  _cbtHeapWriteBitfield(cbtNodeGetRightSibling(node), 0u);
}
void cbtNodeMerge(CBTNode node)
{
  if (!cbtNodeIsRoot(node))
  {
    cbtNodeMergeFast(node);
  }
}
// -----------------------

// --- Node encoding ---
uint cbtNodeEncode(CBTNode node)
{
  uint node_code = 0u;
  CBTNode nodeIter = node;
  while (nodeIter.index > 1u)
  {
    CBTNode leftSibling = cbtNodeGetLeftSiblingFast(nodeIter);
    uint nodeCount = cbtHeapRead(leftSibling);

    node_code += (nodeIter.index & 1u) * nodeCount;
    nodeIter = cbtNodeGetParent(nodeIter);
  }

  return node_code;
}

CBTNode cbtNodeDecode(uint node_code)
{
  CBTNode node = cbtNodeGet(1u, 0);

  while (cbtHeapRead(node) > 1u)
  {
    CBTNode leftChild = cbtNodeGetLeftChildFast(node);
    uint leftChildCount = cbtHeapRead(leftChild);
    uint goingRightBit = node_code < leftChildCount ? 0u : 1u;

    node = leftChild;
    node.index |= goingRightBit;
    node_code -= leftChildCount * goingRightBit;
  }

  return node;
}
// -------------------

#endif // CBT_GLSL_INCLUDED
