/* SPDX-FileCopyrightText: 2020-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "gpu_shader_math_vector_compare.bsl.hh"
#include "gpu_shader_math_vector_reduce.bsl.hh"

float3 calc_barycentric_distances(float3 pos0, float3 pos1, float3 pos2)
{
  float3 edge21 = pos2 - pos1;
  float3 edge10 = pos1 - pos0;
  float3 edge02 = pos0 - pos2;
  float3 d21 = normalize(edge21);
  float3 d10 = normalize(edge10);
  float3 d02 = normalize(edge02);

  float3 dists;
  float d = dot(d21, edge02);
  dists.x = sqrt(dot(edge02, edge02) - d * d);
  d = dot(d02, edge10);
  dists.y = sqrt(dot(edge10, edge10) - d * d);
  d = dot(d10, edge21);
  dists.z = sqrt(dot(edge21, edge21) - d * d);
  return dists;
}

float2 calc_barycentric_co(int vertid)
{
  float2 bary;
  bary.x = float((vertid % 3) == 0);
  bary.y = float((vertid % 3) == 1);
  return bary;
}

/* Assumes GPU_VEC4 is color data, special case that needs luminance coefficients from OCIO. */
#define float_from_float4(v, luminance_coefficients) dot(v.rgb, luminance_coefficients)
#define float_from_float3(v) ((v.r + v.g + v.b) * (1.0f / 3.0f))
#define float_from_float2(v) ((v.x + v.y) * (1.0f / 2.0f))
#define float_from_int(v) float(v)
#define float_from_bool(v) float(v)

#define float2_from_float4(v) v.xy
#define float2_from_float3(v) v.xy
#define float2_from_float(v) float2(v)
#define float2_from_int(v) float2(float(v))
#define float2_from_bool(v) float2(float(v))

#define float3_from_float4(v) v.rgb
#define float3_from_float2(v) float3(v.xy, 0.0f)
#define float3_from_float(v) float3(v)
#define float3_from_int(v) float3(float(v))
#define float3_from_bool(v) float3(float(v))

#define float4_from_float3(v) float4(v, 1.0f)
#define float4_from_float2(v) float4(v.xy, 0.0f, 1.0f)
#define float4_from_float(v) float4(float3(v), 1.0f)
#define float4_from_int(v) float4(float3(float(v)), 1.0f)
#define float4_from_bool(v) float4(float3(float(v)), 1.0f)

#define int_from_float(v) int(v)
#define int_from_bool(v) int(v)
#define int_from_float2(v) int(float_from_float2(v))
#define int_from_float3(v) int_from_float(float_from_float3(v))
/* Assumes GPU_VEC4 is color data, special case that needs luminance coefficients from OCIO. */
#define int_from_float4(v, luminance_coefficients) int(dot(v.rgb, luminance_coefficients))

#define bool_from_float(v) ((v) > 0.0f)
#define bool_from_int(v) ((v) > 0)
#define bool_from_float2(v) (!is_zero(v))
#define bool_from_float3(v) (!is_zero(v))
/* Assumes GPU_VEC4 is color data, special case that needs luminance coefficients from OCIO. */
#define bool_from_float4(v, luminance_coefficients) (dot(v.rgb, luminance_coefficients) > 0.0f)

#ifdef GPU_FRAGMENT_SHADER
#  define FrontFacing gl_FrontFacing
#else
#  define FrontFacing true
#endif

enum ClosureType : uchar {
  CLOSURE_NONE_ID = 0u,
  /* Diffuse */
  CLOSURE_BSDF_DIFFUSE_ID = 1u,
  // CLOSURE_BSDF_OREN_NAYAR_ID = 2u,   /* TODO */
  // CLOSURE_BSDF_SHEEN_ID = 4u,        /* TODO */
  // CLOSURE_BSDF_DIFFUSE_TOON_ID = 5u, /* TODO */
  CLOSURE_BSDF_TRANSLUCENT_ID = 6u,

  /* Glossy */
  CLOSURE_BSDF_MICROFACET_GGX_REFLECTION_ID = 7u,
  // CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID = 8u, /* TODO */
  // CLOSURE_BSDF_ASHIKHMIN_VELVET_ID = 9u,  /* TODO */
  // CLOSURE_BSDF_GLOSSY_TOON_ID = 10u,      /* TODO */
  // CLOSURE_BSDF_HAIR_REFLECTION_ID = 11u,  /* TODO */

  /* Transmission */
  CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID = 12u,
  CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID = 13u,

  /* Glass */
  // CLOSURE_BSDF_HAIR_HUANG_ID = 14u, /* TODO */

  /* BSSRDF */
  CLOSURE_BSSRDF_BURLEY_ID = 15u,
};

struct ClosureUndetermined {
  packed_float3 color;
  packed_float3 N;
  ClosureType type;
  /* Additional data different for each closure type. */
  packed_float4 data;

  float weight() const
  {
    return reduce_add(abs(color));
  }
};

bool closure_has_transmission(const ClosureType closure)
{
  return closure == CLOSURE_BSDF_TRANSLUCENT_ID ||
         closure == CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID ||
         closure == CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID;
}

ClosureUndetermined closure_new(ClosureType type)
{
  ClosureUndetermined cl;
  cl.type = type;
  return cl;
}

struct ClosureOcclusion {
  packed_float3 N;
};

struct ClosureDiffuse {
  packed_float3 color;
  packed_float3 N;
};

struct ClosureSubsurface {
  packed_float3 color;
  packed_float3 N;
  packed_float3 sss_radius;
};

struct ClosureTranslucent {
  packed_float3 color;
  packed_float3 N;
};

struct ClosureReflection {
  packed_float3 color;
  packed_float3 N;
  float roughness;
};

struct ClosureRefraction {
  packed_float3 color;
  packed_float3 N;
  float roughness;
  float ior;
};

struct ClosureHair {
  packed_float3 color;
  packed_float3 T;
  float offset;
  packed_float2 roughness;
};

struct ClosureVolumeScatter {
  packed_float3 scattering;
  float anisotropy;
};

struct ClosureVolumeAbsorption {
  packed_float3 absorption;
};

struct ClosureEmission {
  packed_float3 emission;
};

struct ClosureTransparency {
  packed_float3 transmittance;
  float holdout;
};

struct ClosureThinRefraction {
  packed_float3 color;
  packed_float3 N;
  float roughness;
};

ClosureDiffuse to_closure_diffuse(ClosureUndetermined cl)
{
  ClosureDiffuse closure;
  closure.N = cl.N;
  closure.color = cl.color;
  return closure;
}

ClosureSubsurface to_closure_subsurface(ClosureUndetermined cl)
{
  ClosureSubsurface closure;
  closure.N = cl.N;
  closure.color = cl.color;
  closure.sss_radius = cl.data.xyz;
  return closure;
}

ClosureTranslucent to_closure_translucent(ClosureUndetermined cl)
{
  ClosureTranslucent closure;
  closure.N = cl.N;
  closure.color = cl.color;
  return closure;
}

ClosureReflection to_closure_reflection(ClosureUndetermined cl)
{
  ClosureReflection closure;
  closure.N = cl.N;
  closure.color = cl.color;
  closure.roughness = cl.data.x;
  return closure;
}

ClosureRefraction to_closure_refraction(ClosureUndetermined cl)
{
  ClosureRefraction closure;
  closure.N = cl.N;
  closure.color = cl.color;
  closure.roughness = cl.data.x;
  closure.ior = cl.data.y;
  return closure;
}

ClosureThinRefraction to_closure_thin_refraction(ClosureUndetermined cl)
{
  ClosureThinRefraction closure;
  closure.N = cl.N;
  closure.color = cl.color;
  closure.roughness = cl.data.x;
  return closure;
}

/* Offset of coordinates for evaluating bump node. Unit in pixel. */
float g_derivative_filter_width = 0.0f;
/* Precise derivatives */
int g_derivative_flag = 0;
/* Blur node: offset of the texture coordinates, in the units of the coordinates (used when
 * g_derivative_flag is 2), the radius of the disc in the same units, and a turn of the disc
 * that changes with every pixel and render sample. */
float2 g_enso_blur_offset = float2(0.0f);
float g_enso_blur_radius = 0.0f;
float g_enso_blur_rotation = 0.0f;

float3 dF_impl([[maybe_unused]] float3 v)
{
#ifndef GPU_FRAGMENT_SHADER
  return float3(0.0f);
#elif defined(GPU_FAST_DERIVATIVE) /* TODO(@fclem): User Option? */
  return float3(0.0f);
#else
  if (g_derivative_flag == 2) {
    /* Move inside the texture itself: the two directions the coordinates change in across the
     * surface are made the same length and square to each other, so the blur is round and the
     * same size in the texture from any distance and any angle, like a blur filter on an
     * image. */
    float3 dx = gpu_dfdx(v);
    float3 dy = gpu_dfdy(v);
    float lx = length(dx);
    float ly = length(dy);
    if (lx < ly) {
      float3 swap = dx;
      dx = dy;
      dy = swap;
      lx = ly;
    }
    if (lx < 1e-12f) {
      return float3(0.0f);
    }
    float3 e1 = dx / lx;
    float3 e2 = dy - e1 * dot(dy, e1);
    float l2 = length(e2);
    e2 = (l2 > lx * 1e-4f) ? e2 / l2 : float3(0.0f);
    return e1 * g_enso_blur_offset.x + e2 * g_enso_blur_offset.y;
  }
  if (g_derivative_flag > 0) {
    return gpu_dfdx(v) * g_derivative_filter_width;
  }
  else if (g_derivative_flag < 0) {
    return gpu_dfdy(v) * g_derivative_filter_width;
  }
  return float3(0.0f);
#endif
}

void dF_branch([[maybe_unused]] float fn, [[maybe_unused]] float filter_width, float2 &result)
{
#ifndef GPU_FRAGMENT_SHADER
  result = float2(0.0f);
#elif defined(GPU_FAST_DERIVATIVE) /* TODO(@fclem): User Option? */
  result.x = gpu_dfdx(fn) * filter_width * derivative_scale_get(kg);
  result.y = gpu_dfdy(fn) * filter_width * derivative_scale_get(kg);
#else
#endif
}

void dF_branch_incomplete([[maybe_unused]] float fn,
                          [[maybe_unused]] float filter_width,
                          float2 &result)
{
#ifndef GPU_FRAGMENT_SHADER
  result = float2(0.0f);
#elif defined(GPU_FAST_DERIVATIVE) /* TODO(@fclem): User Option? */
  result.x = gpu_dfdx(fn) * filter_width * derivative_scale_get(kg);
  result.y = gpu_dfdy(fn) * filter_width * derivative_scale_get(kg);
  result += float2(fn);
#endif
}

#if defined(GPU_FRAGMENT_SHADER) && !defined(GPU_FAST_DERIVATIVE)
#  define dF_branch(fn, filter_width, result) \
    if (true) { \
      g_derivative_filter_width = filter_width * derivative_scale_get(kg); \
      g_derivative_flag = 1; \
      result.x = (fn); \
      g_derivative_flag = -1; \
      result.y = (fn); \
      g_derivative_flag = 0; \
      result -= float2((fn)); \
    }

/* Average of `fn` (float4) over a disc in the texture: 24 points on a spiral, soft falloff. */
#  define enso_blur_branch(fn, result) \
    if (true) { \
      int enso_prev_flag = g_derivative_flag; \
      float4 enso_acc = float4(0.0f); \
      float enso_wsum = 0.0f; \
      for (int enso_i = 0; enso_i < 24; enso_i++) { \
        float enso_r = sqrt((float(enso_i) + 0.5f) / 24.0f); \
        float enso_a = float(enso_i) * 2.39996323f + g_enso_blur_rotation; \
        float enso_w = exp(-2.0f * enso_r * enso_r); \
        g_enso_blur_offset = float2(cos(enso_a), sin(enso_a)) * (enso_r * g_enso_blur_radius); \
        g_derivative_flag = 2; \
        enso_acc += (fn) * enso_w; \
        enso_wsum += enso_w; \
      } \
      g_derivative_flag = enso_prev_flag; \
      result = enso_acc / enso_wsum; \
    }

/* Used when the non-offset value is already computed elsewhere */
#  define dF_branch_incomplete(fn, filter_width, result) \
    if (true) { \
      g_derivative_filter_width = filter_width * derivative_scale_get(kg); \
      g_derivative_flag = 1; \
      result.x = (fn); \
      g_derivative_flag = -1; \
      result.y = (fn); \
      g_derivative_flag = 0; \
    }
#else
#  define enso_blur_branch(fn, result) result = (fn);
#endif
