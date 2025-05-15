#version 460

#ifndef LEB_GLSL_INCLUDED
#define LEB_GLSL_INCLUDED

#extension GL_GOOGLE_include_directive : require
#include "cbt.glsl"

struct LEBDiamondParent
{
  CBTNode base;
  CBTNode top;
};

struct _LEBSameNeighbourIndices
{
  uint left;
  uint right;
  uint longestEdge;
  uint node;
}

LEBDiamondParent
_lebGetDiamondParent(CBTNode base, CBTNode top)
{
  return LEBDiamondParent(base, top);
}

_LEBSameNeighbourIndices _lebGetSameSameNeighbourIndices(
  uint left, uint right, uint longestEdge, uint node)
{
  return _LEBSameNeighbourIndices(left, right, longestEdge, node);
}

uint _lebGetBit(uint bit_field, int bit_index)
{
  return ((bit_field >> bit_index) & 1u);
}

// _LEBSameNeighbourIndices _lebNodeSplitIndices(
//   _lebGetSameSameNeighbourIndices node_indices, uint split_bit)
// {
//     uint opposite_bit = split_bit ^ 1u;
    
// }


void main() {}


#endif // LEB_GLSL_INCLUDED
