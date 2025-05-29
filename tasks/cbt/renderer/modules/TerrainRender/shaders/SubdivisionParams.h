#ifndef SUBDIVISION_PARAMS_H_INCLUDED
#define SUBDIVISION_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct SubdivisionParams
{
  shader_mat4 world;
  shader_mat4 view;
  shader_mat4 proj;
  shader_mat4 projView;
  shader_mat4 worldView;
  shader_mat4 worldProjView;

  shader_vec4 frustumPlanes[6];

  shader_float lodFactor;
  shader_float varianceFactor;
  shader_uint tesselationFactor;
  shader_uint texturesAmount;
};


#endif // SUBDIVISION_PARAMS_H_INCLUDED
