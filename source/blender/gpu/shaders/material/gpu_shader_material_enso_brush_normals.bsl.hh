/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō Brush Normals node: painterly normals made of brush strokes.
 *
 * Strokes are scattered in object space (they stick to the object), lie flat on the surface,
 * point along a flow direction with some randomness. Each stroke is a real brush stamp taken from
 * a brush atlas image (a grid of white-on-black brush strokes, drawn left to right), or a built in
 * bristle stroke when no image is set. Overlapping strokes are layered like paint: the stroke
 * painted last covers the others. The paint height becomes a normal through the same surface
 * gradient the Bump node uses. */

#pragma once

#include "gpu_shader_common_hash.bsl.hh"
#include "gpu_shader_material_interface.bsl.hh"

float3 enso_brush_normalize(float3 v)
{
  float l = length(v);
  return (l > 1e-8f) ? v / l : float3(0.0f, 0.0f, 1.0f);
}

struct EnsoBrushParams {
  float3 n;
  float3 flow;
  float stroke_length;
  float stroke_width;
  float bristles;
  float randomness;
  float seed;
  float cols;
  float rows;
  float lod;
};

/* Paint height and the random value of the visible stroke at object-space point q (cells). */
float enso_brush_height(float3 q, const EnsoBrushParams &bp, float &stroke_id)
{
  float3 n = bp.n;
  float3 cell = floor(q);
  float best = 0.0f;
  float best_order = -1.0f;
  stroke_id = 0.0f;
  float3 fl = bp.flow - n * dot(bp.flow, n);
  bool has_flow = dot(fl, fl) > 1e-8f;
  for (int z = -1; z <= 1; z++) {
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        float3 c = cell + float3(float(x), float(y), float(z));
        float3 h = hash_vec3_to_vec3(c + float3(bp.seed, bp.seed * 1.7f, bp.seed * 2.3f));
        float3 center = c + h;
        float3 r = hash_vec3_to_vec3(c + float3(13.1f + bp.seed, 7.7f, 3.3f)) * 2.0f - 1.0f;
        r -= n * dot(r, n);
        float3 d = has_flow ?
                       normalize(mix(normalize(fl), enso_brush_normalize(r), bp.randomness)) :
                       enso_brush_normalize(r);
        d = enso_brush_normalize(d - n * dot(d, n));
        float3 side = cross(n, d);
        float3 v = q - center;
        float along = dot(v, d);
        float across = dot(v, side);
        float len = bp.stroke_length * mix(0.6f, 1.25f, h.x);
        float wid = bp.stroke_width * mix(0.65f, 1.2f, h.y);
        if (abs(along) >= len || abs(across) >= wid) {
          continue;
        }
        float hgt = 0.0f;
        {
          float t = 1.0f - abs(along) / max(len, 1e-4f);
          float ac = abs(across);
          /* Taper: a round head and a dragged tail. */
          float tail = (along < 0.0f) ? smoothstep(0.0f, 0.55f, t) : smoothstep(0.0f, 0.2f, t);
          float edge = smoothstep(wid, wid * 0.55f, ac / max(tail, 0.15f));
          float sx = ac / max(wid, 1e-4f);
          float bristle = 0.5f + 0.5f * sin(sx * bp.bristles * 6.2831853f + h.z * 31.0f +
                                            along * 0.7f);
          float load = mix(0.55f, 1.0f, t);
          hgt = edge * mix(1.0f, bristle, 0.45f) * load;
        }
        if (hgt <= 0.04f) {
          continue;
        }
        float order = h.z;
        if (order > best_order) {
          best_order = order;
          best = hgt;
          stroke_id = fract(h.x * 7.13f + h.y * 3.71f);
        }
      }
    }
  }
  return best;
}

/* Same, with brush stamps from the atlas image. */
float enso_brush_height_image(float3 q,
                              const EnsoBrushParams &bp,
                              sampler2D ima,
                              float &stroke_id)
{
  float3 n = bp.n;
  float3 cell = floor(q);
  float best = 0.0f;
  float best_order = -1.0f;
  stroke_id = 0.0f;
  float3 fl = bp.flow - n * dot(bp.flow, n);
  bool has_flow = dot(fl, fl) > 1e-8f;
  float tiles = max(floor(bp.cols), 1.0f) * max(floor(bp.rows), 1.0f);
  for (int z = -1; z <= 1; z++) {
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        float3 c = cell + float3(float(x), float(y), float(z));
        float3 h = hash_vec3_to_vec3(c + float3(bp.seed, bp.seed * 1.7f, bp.seed * 2.3f));
        float3 center = c + h;
        float3 r = hash_vec3_to_vec3(c + float3(13.1f + bp.seed, 7.7f, 3.3f)) * 2.0f - 1.0f;
        r -= n * dot(r, n);
        float3 d = has_flow ?
                       normalize(mix(normalize(fl), enso_brush_normalize(r), bp.randomness)) :
                       enso_brush_normalize(r);
        d = enso_brush_normalize(d - n * dot(d, n));
        float3 side = cross(n, d);
        float3 v = q - center;
        float along = dot(v, d);
        float across = dot(v, side);
        float len = bp.stroke_length * mix(0.6f, 1.25f, h.x);
        float wid = bp.stroke_width * mix(0.65f, 1.2f, h.y);
        if (abs(along) >= len || abs(across) >= wid) {
          continue;
        }
        float hgt = 0.0f;
        {
          /* Brush stamp: u along the stroke, v across it. */
          float tile = min(floor(fract(h.z * 3.17f + h.x * 1.31f) * tiles), tiles - 1.0f);
          float cols = max(floor(bp.cols), 1.0f);
          float tx = tile - cols * floor(tile / cols);
          float ty = floor(tile / cols);
          float u = along / len * 0.5f + 0.5f;
          float w = across / wid * 0.5f + 0.5f;
          /* Some strokes are painted the other way round. */
          if (h.y > 0.5f) {
            u = 1.0f - u;
          }
          float2 uv = (float2(tx, ty) + float2(u, w)) / float2(cols, max(floor(bp.rows), 1.0f));
          float4 s = textureLod(ima, uv, bp.lod);
          hgt = max(max(s.r, s.g), s.b) * s.a;
        }
        if (hgt <= 0.04f) {
          continue;
        }
        float order = h.z;
        if (order > best_order) {
          best_order = order;
          best = hgt;
          stroke_id = fract(h.x * 7.13f + h.y * 3.71f);
        }
      }
    }
  }
  return best;
}

/* Stroke parameters in object space. */
EnsoBrushParams enso_brush_params(const ObjectMatrices &obm,
                                  float3 N,
                                  float stroke_length,
                                  float stroke_width,
                                  float bristles,
                                  float randomness,
                                  float3 flow,
                                  float seed,
                                  float atlas_columns,
                                  float atlas_rows)
{
  EnsoBrushParams bp;
  /* Object-space normal: world to object normals use the transposed model matrix. */
  bp.n = enso_brush_normalize((float4(N, 0.0f) * obm.model).xyz);
  bp.flow = (obm.model_inverse * float4(flow, 0.0f)).xyz;
  bp.stroke_length = stroke_length;
  bp.stroke_width = stroke_width;
  bp.bristles = bristles;
  bp.randomness = randomness;
  bp.seed = seed;
  bp.cols = max(floor(atlas_columns), 1.0f);
  bp.rows = max(floor(atlas_rows), 1.0f);
  bp.lod = 0.0f;
  return bp;
}

/* Paint heights at the point and one pixel to the side become the bumped normal. */
float3 enso_brush_bump(const ShadingData &sd,
                       float3 N,
                       float scale,
                       float strength,
                       float h,
                       float hx,
                       float hy)
{
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float3 dPdx = gpu_dfdx(sd.P);
  float3 dPdy = gpu_dfdy(sd.P);
  /* Surface gradient, as in the Bump node; paint thickness relative to the stroke size. */
  float3 Rx = cross(dPdy, N);
  float3 Ry = cross(N, dPdx);
  float det = dot(dPdx, Rx);
  float3 surfgrad = (hx - h) * Rx + (hy - h) * Ry;
  float dist = 0.1f / scale;
  float3 bumped = normalize(abs(det) * N - dist * sign(det) * surfgrad);
  return normalize(mix(N, bumped, saturate(strength)));
#else
  return N;
#endif
}

[[node]]
void node_enso_brush_normals(float scale,
                             float stroke_length,
                             float stroke_width,
                             float strength,
                             float bristles,
                             float randomness,
                             float3 flow,
                             float seed,
                             float atlas_columns,
                             float atlas_rows,
                             float3 N,
                             [[resource_table]] KernelGlobals &kg,
                             const ShadingData &sd,
                             float3 &normal,
                             float &height,
                             float &stroke)
{
  N = normalize(N);
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float s = max(scale, 1e-4f);
  float3 Po = (obm.model_inverse * float4(sd.P, 1.0f)).xyz;
  EnsoBrushParams bp = enso_brush_params(obm, N, stroke_length, stroke_width, bristles,
                                         randomness, flow, seed, atlas_columns, atlas_rows);
  float sid;
  height = enso_brush_height(Po * s, bp, sid);
  stroke = sid;
  float hx = height;
  float hy = height;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float3 dPox = (obm.model_inverse * float4(gpu_dfdx(sd.P), 0.0f)).xyz;
  float3 dPoy = (obm.model_inverse * float4(gpu_dfdy(sd.P), 0.0f)).xyz;
  float t;
  hx = enso_brush_height((Po + dPox) * s, bp, t);
  hy = enso_brush_height((Po + dPoy) * s, bp, t);
#endif
  normal = enso_brush_bump(sd, N, s, strength, height, hx, hy);
}

/* Same node with a brush atlas image. */
[[node]]
void node_enso_brush_normals_image(float scale,
                                   float stroke_length,
                                   float stroke_width,
                                   float strength,
                                   float bristles,
                                   float randomness,
                                   float3 flow,
                                   float seed,
                                   float atlas_columns,
                                   float atlas_rows,
                                   float3 N,
                                   [[resource_table]] KernelGlobals &kg,
                                   const ShadingData &sd,
                                   sampler2D ima,
                                   float3 &normal,
                                   float &height,
                                   float &stroke)
{
  N = normalize(N);
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float s = max(scale, 1e-4f);
  float3 Po = (obm.model_inverse * float4(sd.P, 1.0f)).xyz;
  EnsoBrushParams bp = enso_brush_params(obm, N, stroke_length, stroke_width, bristles,
                                         randomness, flow, seed, atlas_columns, atlas_rows);
  #if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float3 dPox = (obm.model_inverse * float4(gpu_dfdx(sd.P), 0.0f)).xyz;
  float3 dPoy = (obm.model_inverse * float4(gpu_dfdy(sd.P), 0.0f)).xyz;
  /* Mip level from how many stamp texels one pixel covers: no shimmer at a distance. */
  float cells_px = max(length(dPox), length(dPoy)) * s;
  float2 tex = float2(textureSize(ima, 0).xy);
  float2 tile_tx = tex / float2(bp.cols, bp.rows);
  float uv_px = cells_px / max(2.0f * min(stroke_length, stroke_width), 1e-4f);
  bp.lod = max(log2(max(uv_px * max(tile_tx.x, tile_tx.y), 1e-6f)), 0.0f);
#endif
  float sid;
  height = enso_brush_height_image(Po * s, bp, ima, sid);
  stroke = sid;
  float hx = height;
  float hy = height;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float t;
  hx = enso_brush_height_image((Po + dPox) * s, bp, ima, t);
  hy = enso_brush_height_image((Po + dPoy) * s, bp, ima, t);
#endif
  normal = enso_brush_bump(sd, N, s, strength, height, hx, hy);
}
