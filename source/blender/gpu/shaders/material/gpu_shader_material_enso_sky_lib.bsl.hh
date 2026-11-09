/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō Anime Sky: 4D FBM noise, same math and same operation order as the Noise Texture node
 * (node_noise_tex_fbm_3d / node_noise_tex_fbm_4d), written so GPU drivers compile it quickly.
 *
 * The node library evaluates the 16 Perlin corners as one long straight-line expression. Many of
 * them in one shader make the Vulkan driver spend minutes compiling. Here the corners are
 * evaluated in a short loop whose trip count the compiler cannot prove (it is always 4), so the
 * code stays small. The arithmetic per corner and the interpolation order are unchanged. */

#pragma once

#include "gpu_shader_material_noise.bsl.hh"
#include "gpu_shader_common_color_utils.bsl.hh"
#include "gpu_shader_material_tex_image.bsl.hh"
#include "gpu_shader_material_tex_noise.bsl.hh"

float enso_sky_perlin4(float4 vec)
{
  int X, Y, Z, W;
  float fx, fy, fz, fw;

  floor_frac(vec.x, X, fx);
  floor_frac(vec.y, Y, fy);
  floor_frac(vec.z, Z, fz);
  floor_frac(vec.w, W, fw);

  float u = fade(fx);
  float v = fade(fy);
  float t = fade(fz);
  float s = fade(fw);

  /* bi_mix() of the 4 corners of each (z, w) face. */
  float b00 = 0.0f;
  float b10 = 0.0f;
  float b01 = 0.0f;
  float b11 = 0.0f;
  /* Always 4: x - floor(x) is never above 1. */
  int faces = 4 - int(fx > 1.0f);
  for (int k = 0; k < faces; k++) {
    int dz = k & 1;
    int dw = k >> 1;
    float gz = fz - float(dz);
    float gw = fw - float(dw);
    float b = bi_mix(noise_grad(hash_int4(X, Y, Z + dz, W + dw), fx, fy, gz, gw),
                     noise_grad(hash_int4(X + 1, Y, Z + dz, W + dw), fx - 1.0f, fy, gz, gw),
                     noise_grad(hash_int4(X, Y + 1, Z + dz, W + dw), fx, fy - 1.0f, gz, gw),
                     noise_grad(
                         hash_int4(X + 1, Y + 1, Z + dz, W + dw), fx - 1.0f, fy - 1.0f, gz, gw),
                     u,
                     v);
    if (k == 0) {
      b00 = b;
    }
    else if (k == 1) {
      b10 = b;
    }
    else if (k == 2) {
      b01 = b;
    }
    else {
      b11 = b;
    }
  }
  /* quad_mix(): mix(tri_mix(w = 0), tri_mix(w = 1), s), tri_mix = (1 - z) * near + z * far. */
  float t1 = 1.0f - t;
  return mix(t1 * b00 + t * b10, t1 * b01 + t * b11, s);
}

float enso_sky_snoise4(float4 p)
{
  float4 precision_correction = 0.5f * float4(float(abs(p.x) >= 1000000.0f),
                                              float(abs(p.y) >= 1000000.0f),
                                              float(abs(p.z) >= 1000000.0f),
                                              float(abs(p.w) >= 1000000.0f));
  p = compatible_mod(p, 100000.0f) + precision_correction;
  return noise_scale4(enso_sky_perlin4(p));
}

float enso_sky_fbm4(float4 co, float detail, float roughness, float lacunarity, bool normalize)
{
  float4 p = co;
  float fscale = 1.0f;
  float amp = 1.0f;
  float maxamp = 0.0f;
  float sum = 0.0f;

  /* Same octave count; the extra term is always 0 but keeps the loop from being unrolled. */
  int octaves = int(detail) - int(fract(co.x) > 1.0f);
  for (int i = 0; i <= octaves; i++) {
    float t = enso_sky_snoise4(fscale * p);
    sum += t * amp;
    maxamp += amp;
    amp *= roughness;
    fscale *= lacunarity;
  }
  float rmd = detail - floor(detail);
  if (rmd != 0.0f) {
    float t = enso_sky_snoise4(fscale * p);
    float sum2 = sum + t * amp;
    return normalize ? mix(0.5f * sum / maxamp + 0.5f, 0.5f * sum2 / (maxamp + amp) + 0.5f, rmd) :
                       mix(sum, sum2, rmd);
  }
  return normalize ? 0.5f * sum / maxamp + 0.5f : sum;
}

/* node_noise_tex_fbm_4d() with noise_fractal_distorted<float4, NoiseFBM<float4>>. */
void enso_sky_noise_fbm_4d(float3 co,
                           float w,
                           float scale,
                           float detail,
                           float roughness,
                           float lacunarity,
                           float /*offset*/,
                           float /*gain*/,
                           float distortion,
                           float normalize,
                           float compute_color,
                           float &value,
                           float4 &color)
{
  float4 p = float4(co, w) * scale;
  detail = clamp(detail, 0.0f, 15.0f);
  roughness = max(roughness, 0.0f);

  p = distort_point(p, distortion);

  bool norm = (normalize != 0.0f);
  value = enso_sky_fbm4(p, detail, roughness, lacunarity, norm);

  if (compute_color != 0.0f) {
    float seed_base = color_seed_offset(p);
    color = float4(value,
                   enso_sky_fbm4(p + random_vec4_offset(seed_base + 0.0f),
                                 detail,
                                 roughness,
                                 lacunarity,
                                 norm),
                   enso_sky_fbm4(p + random_vec4_offset(seed_base + 1.0f),
                                 detail,
                                 roughness,
                                 lacunarity,
                                 norm),
                   1.0f);
  }
}

/* 3D: same as noise_perlin(float3) / snoise(float3) / noise_fbm<float3>. */
float enso_sky_perlin3(float3 vec)
{
  int X, Y, Z;
  float fx, fy, fz;

  floor_frac(vec.x, X, fx);
  floor_frac(vec.y, Y, fy);
  floor_frac(vec.z, Z, fz);

  float u = fade(fx);
  float v = fade(fy);
  float w = fade(fz);

  float b0 = 0.0f;
  float b1 = 0.0f;
  int faces = 2 - int(fx > 1.0f);
  for (int k = 0; k < faces; k++) {
    float gz = fz - float(k);
    float b = bi_mix(noise_grad(hash_int3(X, Y, Z + k), fx, fy, gz),
                     noise_grad(hash_int3(X + 1, Y, Z + k), fx - 1.0f, fy, gz),
                     noise_grad(hash_int3(X, Y + 1, Z + k), fx, fy - 1.0f, gz),
                     noise_grad(hash_int3(X + 1, Y + 1, Z + k), fx - 1.0f, fy - 1.0f, gz),
                     u,
                     v);
    if (k == 0) {
      b0 = b;
    }
    else {
      b1 = b;
    }
  }
  /* tri_mix(): (1 - z) * near + z * far. */
  return (1.0f - w) * b0 + w * b1;
}

float enso_sky_snoise3(float3 p)
{
  float3 precision_correction = 0.5f * float3(float(abs(p.x) >= 1000000.0f),
                                              float(abs(p.y) >= 1000000.0f),
                                              float(abs(p.z) >= 1000000.0f));
  p = compatible_mod(p, 100000.0f) + precision_correction;
  return noise_scale3(enso_sky_perlin3(p));
}

float enso_sky_fbm3(float3 co, float detail, float roughness, float lacunarity, bool normalize)
{
  float3 p = co;
  float fscale = 1.0f;
  float amp = 1.0f;
  float maxamp = 0.0f;
  float sum = 0.0f;

  int octaves = int(detail) - int(fract(co.x) > 1.0f);
  for (int i = 0; i <= octaves; i++) {
    float t = enso_sky_snoise3(fscale * p);
    sum += t * amp;
    maxamp += amp;
    amp *= roughness;
    fscale *= lacunarity;
  }
  float rmd = detail - floor(detail);
  if (rmd != 0.0f) {
    float t = enso_sky_snoise3(fscale * p);
    float sum2 = sum + t * amp;
    return normalize ? mix(0.5f * sum / maxamp + 0.5f, 0.5f * sum2 / (maxamp + amp) + 0.5f, rmd) :
                       mix(sum, sum2, rmd);
  }
  return normalize ? 0.5f * sum / maxamp + 0.5f : sum;
}

/* node_noise_tex_fbm_3d() with noise_fractal_distorted<float3, NoiseFBM<float3>>. */
void enso_sky_noise_fbm_3d(float3 co,
                           float /*w*/,
                           float scale,
                           float detail,
                           float roughness,
                           float lacunarity,
                           float /*offset*/,
                           float /*gain*/,
                           float distortion,
                           float normalize,
                           float compute_color,
                           float &value,
                           float4 &color)
{
  float3 p = co * scale;
  detail = clamp(detail, 0.0f, 15.0f);
  roughness = max(roughness, 0.0f);

  p = distort_point(p, distortion);

  bool norm = (normalize != 0.0f);
  value = enso_sky_fbm3(p, detail, roughness, lacunarity, norm);

  if (compute_color != 0.0f) {
    float seed_base = color_seed_offset(p);
    color = float4(value,
                   enso_sky_fbm3(p + random_vec3_offset(seed_base + 0.0f),
                                 detail,
                                 roughness,
                                 lacunarity,
                                 norm),
                   enso_sky_fbm3(p + random_vec3_offset(seed_base + 1.0f),
                                 detail,
                                 roughness,
                                 lacunarity,
                                 norm),
                   1.0f);
  }
}

/* Image Texture node (Linear interpolation, flat projection) as the sky samples it.
 * mode: 0 no image (node_tex_image_empty), 1 alpha cleared, 2 unpremultiplied, 3 premultiplied,
 * 4 color unchanged. The node chooses the mode exactly like the Image Texture node does. */
void enso_sky_sample(float3 co,
                     sampler2D ima,
                     float mode,
                     [[resource_table]] KernelGlobals &kg,
                     float4 &color,
                     float &alpha)
{
  if (mode == 0.0f) {
    color = float4(0.0f);
    alpha = 1.0f;
    return;
  }
  node_tex_image_linear(co, ima, kg, color, alpha);
  if (mode == 1.0f) {
    color_alpha_clear(color, color);
  }
  else if (mode == 2.0f) {
    color_alpha_unpremultiply(color, color);
  }
  else if (mode == 3.0f) {
    color_alpha_premultiply(color, color);
  }
}
