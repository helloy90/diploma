#ifndef LEB_GLSL_INCLUDED
#define LEB_GLSL_INCLUDED

#extension GL_GOOGLE_include_directive : require

#include "cbt.glsl"

struct LEBDiamondParent
{
  CBTNode bottom;
  CBTNode top;
};

struct _LEBSameNeighbourIndices
{
  uint left;
  uint right;
  uint longestEdge;
  uint node;
};

LEBDiamondParent _lebGetDiamondParent(CBTNode bottom, CBTNode top)
{
  return LEBDiamondParent(bottom, top);
}

_LEBSameNeighbourIndices _lebGetSameNeighbourIndices(
  uint left, uint right, uint longestEdge, uint node)
{
  return _LEBSameNeighbourIndices(left, right, longestEdge, node);
}

uint _lebGetBit(uint bit_field, int bit_index)
{
  return ((bit_field >> bit_index) & 1u);
}

_LEBSameNeighbourIndices _lebSplitIndices(_LEBSameNeighbourIndices node_indices, uint path_bit)
{
  uint siblingPathBit = path_bit ^ 1u;
  bool siblingPathTaken = bool(siblingPathBit);
  uint array[4] = {
    node_indices.left, node_indices.right, node_indices.longestEdge, node_indices.node};

  return _LEBSameNeighbourIndices(
    (array[2 + path_bit] << 1u) | uint(siblingPathTaken && bool(array[2 + path_bit])),
    (array[2 + siblingPathBit] << 1u) | uint(siblingPathTaken && bool(array[2 + siblingPathBit])),
    (array[path_bit] << 1u) | uint(siblingPathTaken && bool(array[path_bit])),
    (array[3] << 1u) | path_bit);
}

_LEBSameNeighbourIndices lebDecodeSameDepthNeighbours(CBTNode node)
{
  _LEBSameNeighbourIndices indices = _lebGetSameNeighbourIndices(0u, 0u, 0u, 1u);

  for (int bitIndex = node.depth - 1; bitIndex >= 0; bitIndex--)
  {
    // traversing path from root to node
    indices = _lebSplitIndices(indices, _lebGetBit(node.index, bitIndex));
  }

  return indices;
}

_LEBSameNeighbourIndices lebSquareDecodeSameDepthNeighbours(CBTNode node)
{
  uint isSecondTriangle = _lebGetBit(node.index, max(0, node.depth - 1));
  _LEBSameNeighbourIndices indices =
    _lebGetSameNeighbourIndices(0u, 0u, 3u - isSecondTriangle, 2u + isSecondTriangle);

  for (int bitIndex = node.depth - 2; bitIndex >= 0; bitIndex--)
  {
    // traversing path from root to node
    indices = _lebSplitIndices(indices, _lebGetBit(node.index, bitIndex));
  }

  return indices;
}

CBTNode _lebEdgeNeighbour(CBTNode node)
{
  uint node_index = lebDecodeSameDepthNeighbours(node).longestEdge;

  return cbtNodeGet(node_index, (node_index == 0u) ? 0 : node.depth);
}

CBTNode _lebSquareEdgeNeighbour(CBTNode node)
{
  uint node_index = lebSquareDecodeSameDepthNeighbours(node).longestEdge;

  return cbtNodeGet(node_index, (node_index == 0u) ? 0 : node.depth);
}

void lebNodeSplit(CBTNode node)
{
  if (cbtNodeIsDeepestLeaf(node))
  {
    return;
  }

  CBTNode nodeIterator = node;
  cbtNodeSplit(nodeIterator);
  nodeIterator = _lebEdgeNeighbour(nodeIterator);

  while (nodeIterator.index > 1u)
  {
    cbtNodeSplit(nodeIterator);
    nodeIterator = cbtNodeGetParentFast(nodeIterator);
    cbtNodeSplit(nodeIterator);
    nodeIterator = _lebEdgeNeighbour(nodeIterator);
  }
}

void lebSquareNodeSplit(CBTNode node)
{
  if (cbtNodeIsDeepestLeaf(node))
  {
    return;
  }

  CBTNode nodeIterator = node;
  cbtNodeSplit(nodeIterator);
  nodeIterator = _lebSquareEdgeNeighbour(nodeIterator);

  while (nodeIterator.index > 1u)
  {
    cbtNodeSplit(nodeIterator);
    nodeIterator = cbtNodeGetParentFast(nodeIterator);
    if (nodeIterator.index > 1u)
    {
      cbtNodeSplit(nodeIterator);
      nodeIterator = _lebSquareEdgeNeighbour(nodeIterator);
    }
  }
}

LEBDiamondParent lebDiamondParentDecode(CBTNode node)
{
  CBTNode parent = cbtNodeGetParentFast(node);
  uint longestEdgeNeighbourIndex = lebDecodeSameDepthNeighbours(parent).longestEdge;
  CBTNode longestEdgeNeighbourNode = cbtNodeGet(
    longestEdgeNeighbourIndex > 0u ? longestEdgeNeighbourIndex : parent.index, parent.depth);

  return _lebGetDiamondParent(parent, longestEdgeNeighbourNode);
}

LEBDiamondParent lebSquareDiamondParentDecode(CBTNode node)
{
  CBTNode parent = cbtNodeGetParentFast(node);
  uint longestEdgeNeighbourIndex = lebSquareDecodeSameDepthNeighbours(parent).longestEdge;
  CBTNode longestEdgeNeighbourNode = cbtNodeGet(
    longestEdgeNeighbourIndex > 0u ? longestEdgeNeighbourIndex : parent.index, parent.depth);

  return _lebGetDiamondParent(parent, longestEdgeNeighbourNode);
}

bool _lebHasDiamondParent(LEBDiamondParent diamond_parent)
{
  bool canMergeTop = cbtHeapRead(diamond_parent.top) <= 2u;
  bool canMergeBottom = cbtHeapRead(diamond_parent.bottom) <= 2u;

  return canMergeTop && canMergeBottom;
}

void lebNodeMerge(CBTNode node, LEBDiamondParent diamond_parent)
{
  if (!cbtNodeIsRoot(node) && _lebHasDiamondParent(diamond_parent))
  {
    cbtNodeMerge(node);
  }
}

void lebSquareNodeMerge(CBTNode node, LEBDiamondParent diamond_parent)
{
  if ((node.depth > 1) && _lebHasDiamondParent(diamond_parent))
  {
    cbtNodeMerge(node);
  }
}

mat3x3 _lebGetSplitMatrix(uint split_bit)
{
  float bit = float(split_bit);
  float oppositeBit = 1.0 - bit;
  return transpose(mat3x3(oppositeBit, bit, 0.0, 0.5, 0.0, 0.5, 0.0, oppositeBit, bit));
}

mat3x3 _lebGetSquareMatrix(uint quad_bit)
{
  float bit = float(quad_bit);
  float oppositeBit = 1.0 - bit;

  return transpose(mat3x3(oppositeBit, 0.0, bit, bit, oppositeBit, bit, bit, 0.0, oppositeBit));
}

mat3x3 _lebGetMirrorMatrix(uint mirror_bit)
{
  float bit = float(mirror_bit);
  float oppositeBit = 1.0 - bit;

  return mat3x3(oppositeBit, 0.0, bit, 0, 1.0, 0, bit, 0.0, oppositeBit);
}

mat3x3 _lebDecodeTransformationMatrix(CBTNode node)
{
  mat3x3 transform = mat3x3(1.0);

  for (int bitIndex = node.depth - 1; bitIndex >= 0; bitIndex--)
  {
    transform = _lebGetSplitMatrix(_lebGetBit(node.index, bitIndex)) * transform;
  }
  transform = _lebGetMirrorMatrix(node.depth & 1) * transform;

  return transform;
}

mat3x3 _lebSquareDecodeTransformationMatrix(CBTNode node)
{
  // int bitIndex = max(0, node.depth - 1);

  // mat3x3 transform = _lebGetSquareMatrix(_lebGetBit(node.index, bitIndex));
  uint isSecondTriangle = _lebGetBit(node.index, max(0, node.depth - 1));

  mat3x3 transform = _lebGetSquareMatrix(isSecondTriangle);

  for (int bitIndex = node.depth - 2; bitIndex >= 0; bitIndex--)
  {
    transform = _lebGetSplitMatrix(_lebGetBit(node.index, bitIndex)) * transform;
  }
  transform = _lebGetMirrorMatrix((node.depth ^ 1) & 1) * transform;

  return transform;
}


vec3 lebNodeDecodeAttribute(CBTNode node, vec3 data) {
  return _lebDecodeTransformationMatrix(node) * data;
}

mat2x3 lebNodeDecodeAttribute(CBTNode node, mat2x3 data) {
  return _lebDecodeTransformationMatrix(node) * data;
}

mat3x3 lebNodeDecodeAttribute(CBTNode node, mat3x3 data) {
  return _lebDecodeTransformationMatrix(node) * data;
}

mat4x3 lebNodeDecodeAttribute(CBTNode node, mat4x3 data) {
  return _lebDecodeTransformationMatrix(node) * data;
}

vec3 lebSquareNodeDecodeAttribute(CBTNode node, vec3 data) {
  return _lebSquareDecodeTransformationMatrix(node) * data;
}

mat2x3 lebSquareNodeDecodeAttribute(CBTNode node, mat2x3 data) {
  return _lebSquareDecodeTransformationMatrix(node) * data;
}

mat3x3 lebSquareNodeDecodeAttribute(CBTNode node, mat3x3 data) {
  return _lebSquareDecodeTransformationMatrix(node) * data;
}

mat4x3 lebSquareNodeDecodeAttribute(CBTNode node, mat4x3 data) {
  return _lebSquareDecodeTransformationMatrix(node) * data;
}

#endif // LEB_GLSL_INCLUDED
