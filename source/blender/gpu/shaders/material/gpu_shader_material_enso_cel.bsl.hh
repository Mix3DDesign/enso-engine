/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō clean cel shading.
 *
 *   Ensō Cel Bands       shadow steps whose edge is smoothed over a fixed number of screen
 *                        pixels (from the screen-space derivative of the value), so the line
 *                        between light and shadow is crisp at any zoom and never stair-steps.
 *   Ensō Smooth Normals  normals blended toward a ball or tube around the object, the same
 *                        idea as transferring normals from a proxy shape, done per pixel.
 *   Ensō Face Shadow     the anime face shadow: light and rounded normal are levelled onto the
 *                        head's horizontal plane, so the shadow only depends on how far the
 *                        light has turned around the head. Also gives the threshold and mirror
 *                        flag used by drawn face shadow maps. */

#pragma once

#include "gpu_shader_material_interface.bsl.hh"

#include "gpu_shader_material_enso_npr.bsl.hh"

float3 enso_cel_normalize(float3 v, float3 fallback)
{
  float l = length(v);
  return (l > 1e-8f) ? v / l : fallback;
}

/* Smoothed step: `edge_px` screen pixels wide, widened by `softness`. */
float enso_cel_step(float threshold, float value, float softness, float edge_px)
{
  float w = softness * 0.5f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float fw = abs(gpu_dfdx(value)) + abs(gpu_dfdy(value));
  w = max(w, fw * edge_px * 0.5f);
#endif
  w = max(w, 1e-5f);
  return smoothstep(threshold - w, threshold + w, value);
}

/* Lighting for Cel Bands when nothing is plugged into Value: plain facing toward the light. */
[[node]]
void node_enso_cel_own_light(float3 normal, float3 light_dir, float &value)
{
  float3 L = enso_cel_normalize(light_dir, float3(0.0f, 0.0f, 1.0f));
  value = saturate(dot(normalize(normal), L));
}

[[node]]
void node_enso_cel_bands(float value,
                         float bands,
                         float threshold,
                         float spread,
                         float softness,
                         float edge_px,
                         float3 /*light_dir*/,
                         float3 /*normal*/,
                         float &shade,
                         float &edge)
{
  int n = int(clamp(floor(bands + 0.5f), 1.0f, 8.0f));
  float total = 0.0f;
  float line = 0.0f;
  for (int i = 0; i < n; i++) {
    float t = threshold + (float(i) - float(n - 1) * 0.5f) * spread;
    float s = enso_cel_step(t, value, softness, edge_px);
    total += s;
    /* 1 in the middle of the transition, 0 on both sides. */
    line = max(line, 4.0f * s * (1.0f - s));
  }
  shade = total / float(n);
  edge = line;
}

/* Normal of a ball / tube around `center`, in world space. */
float3 enso_round_normal(const ObjectMatrices &obm,
                         float3 P,
                         float3 N,
                         float3 center,
                         float3 stretch,
                         float tube)
{
  float3 Po = (obm.model_inverse * float4(P, 1.0f)).xyz;
  float3 inv = float3(1.0f) / max(abs(stretch), float3(1e-4f));
  /* Gradient of the ellipsoid ((p - c) / stretch)^2. */
  float3 d = (Po - center) * inv * inv;
  d.z *= (1.0f - saturate(tube));
  float3 No = (float4(N, 0.0f) * obm.model).xyz;
  d = enso_cel_normalize(d, enso_cel_normalize(No, float3(0.0f, 0.0f, 1.0f)));
  /* Object to world for normals: transposed inverse model matrix. */
  return enso_cel_normalize((float4(d, 0.0f) * obm.model_inverse).xyz, N);
}

[[node]]
void node_enso_smooth_normals(float roundness,
                              float3 center,
                              float3 stretch,
                              float tube,
                              float3 N,
                              [[resource_table]] KernelGlobals &kg,
                              const ShadingData &sd,
                              float3 &normal)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 R = enso_round_normal(obm, sd.P, N, center, stretch, tube);
  normal = enso_cel_normalize(mix(N, R, saturate(roundness)), N);
}

[[node]]
void node_enso_face_shadow(float3 light_dir,
                           float3 forward,
                           float3 up,
                           float roundness,
                           float3 center,
                           float level,
                           float offset,
                           float softness,
                           float edge_px,
                           float3 N,
                           [[resource_table]] KernelGlobals &kg,
                           const ShadingData &sd,
                           float &shade,
                           float &map_threshold,
                           float &flip,
                           float3 &normal)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 Uw = enso_cel_normalize((obm.model * float4(up, 0.0f)).xyz, float3(0.0f, 0.0f, 1.0f));
  float3 Fw = (obm.model * float4(forward, 0.0f)).xyz;
  Fw = enso_cel_normalize(Fw - Uw * dot(Fw, Uw), float3(0.0f, -1.0f, 0.0f));
  float3 Rw = cross(Uw, Fw);

  float lv = saturate(level);
  float3 L = enso_cel_normalize(light_dir, Fw);
  /* Level the light onto the head's horizontal plane. A light straight above falls back to
   * the front so the face stays lit. */
  float3 Lh = enso_cel_normalize(L - Uw * dot(L, Uw) * lv, Fw);

  float3 Nr = enso_round_normal(obm, sd.P, N, center, float3(1.0f), 0.0f);
  Nr = enso_cel_normalize(mix(N, Nr, saturate(roundness)), N);
  normal = Nr;
  float3 Nh = enso_cel_normalize(Nr - Uw * dot(Nr, Uw) * lv, Nr);

  float ndl = dot(Nh, Lh);
  shade = enso_cel_step(offset, ndl, softness, edge_px);

  /* Drawn face shadow maps store, per pixel, how far the light must turn before the pixel
   * goes dark: lit when map > 0.5 - 0.5 * (forward . light). */
  map_threshold = saturate(0.5f - 0.5f * dot(Fw, Lh) + offset * 0.5f);
  flip = (dot(Rw, Lh) < 0.0f) ? 1.0f : 0.0f;
}

/* Ensō Hair Ring: the "angel ring" highlight. The hair is treated as strands running from the
 * top of the head down a ball around `center`; the ring sits where those strands are square
 * to the half vector, and `strands` pushes it up and down per lock for the zigzag look. */
[[node]]
void node_enso_hair_ring(float3 light_dir,
                         float3 up,
                         float3 center,
                         float position,
                         float width,
                         float strands,
                         float strand_amount,
                         float locks,
                         float follow_light,
                         float softness,
                         float edge_px,
                         float3 N,
                         [[resource_table]] KernelGlobals &kg,
                         ShadingData &sd,
                         float &ring,
                         float &offset,
                         float3 &tangent)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 Uw = enso_cel_normalize((obm.model * float4(up, 0.0f)).xyz, float3(0.0f, 0.0f, 1.0f));
  float3 Nr = enso_round_normal(obm, sd.P, N, center, float3(1.0f), 0.0f);
  float3 T = enso_cel_normalize(Uw - Nr * dot(Uw, Nr), Uw);
  tangent = T;

  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 L = enso_cel_normalize(light_dir, V);
  float3 H = enso_cel_normalize(mix(V, enso_cel_normalize(L + V, V), saturate(follow_light)), V);

  float shift = position + (strands - 0.5f) * strand_amount;
  if (locks >= 1.0f) {
    /* Built-in zigzag: split the head into locks around the up axis, each lock is a spike
     * with its own height. */
    float3 Po = (obm.model_inverse * float4(sd.P, 1.0f)).xyz - center;
    float3 Uo = enso_cel_normalize(up, float3(0.0f, 0.0f, 1.0f));
    float3 ref = (abs(Uo.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 Xo = enso_cel_normalize(cross(Uo, ref), float3(1.0f, 0.0f, 0.0f));
    float3 Yo = cross(Uo, Xo);
    float n = floor(locks);
    float u = (atan(dot(Po, Yo), dot(Po, Xo)) / (2.0f * M_PI) + 0.5f) * n;
    float cell = floor(u);
    float f = u - cell;
    float rnd = fract(sin(cell * 12.9898f + 4.1f) * 43758.5453f);
    float tri = 1.0f - abs(2.0f * f - 1.0f);
    shift += (tri * (0.35f + 0.65f * rnd) - 0.35f) * strand_amount;
  }
  float3 Ts = enso_cel_normalize(T + Nr * shift, T);
  offset = dot(Ts, H);
  float v = 1.0f - abs(offset);
  float r = enso_cel_step(1.0f - max(width, 0.0f), v, softness, edge_px);
  /* Only on the side that faces the viewer and the light. */
  r *= enso_cel_step(0.0f, dot(Nr, H), 0.1f, edge_px);
  ring = r;
}

/* Ensō Face Map: reads a face shadow map. Red holds the light coming from the character's
 * right, green from the left (a grey map works for both). The pixel is lit while the map is
 * above the threshold given by Ensō Face Shadow; the edge is smoothed in screen space so a
 * low resolution or 8 bit map still gives a clean line. */
[[node]]
void node_enso_face_map(float4 map,
                        float threshold,
                        float flip,
                        float softness,
                        float edge_px,
                        float &shade)
{
  float v = mix(map.r, map.g, saturate(flip));
  shade = enso_cel_step(threshold, v, softness, edge_px);
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Mask: select part of a surface. */
[[node]]
void node_enso_mask(float3 center,
                    float3 size,
                    float roundness,
                    float height_min,
                    float height_max,
                    float3 up,
                    float slope_angle,
                    float world_space,
                    float softness,
                    float edge_px,
                    float3 N,
                    [[resource_table]] KernelGlobals &kg,
                    ShadingData &sd,
                    float &shape,
                    float &height,
                    float &slope,
                    float &facing)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  ObjectMatrices obm = kg.object_matrices_get(sd);
  bool world = world_space > 0.5f;
  float3 p = world ? sd.P : (obm.model_inverse * float4(sd.P, 1.0f)).xyz;
  float3 n = world ? N : enso_cel_normalize((float4(N, 0.0f) * obm.model).xyz, N);
  float3 U = enso_cel_normalize(up, float3(0.0f, 0.0f, 1.0f));

  float3 q = abs((p - center) / max(abs(size), float3(1e-6f)));
  float d_box = max(q.x, max(q.y, q.z));
  float d = mix(d_box, length(q), saturate(roundness));
  shape = 1.0f - enso_cel_step(1.0f, d, softness, edge_px);

  float h = dot(p, U);
  float range = height_max - height_min;
  if (abs(range) < 1e-5f) {
    height = enso_cel_step(height_min, h, softness, edge_px);
  }
  else {
    height = saturate((h - height_min) / range);
  }

  slope = enso_cel_step(cos(clamp(slope_angle, 0.0f, 3.14159f)), dot(n, U), softness, edge_px);

  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  facing = saturate(dot(N, V));
}

/* Ensō Shade Info: one light split into lit side, form shadow and cast shadow. */
[[node]]
void node_enso_shade_info(float3 light_dir,
                          float mask,
                          float terminator,
                          float wrap,
                          float softness,
                          float edge_px,
                          float3 N,
                          float shadow_distance,
                          [[resource_table]] KernelGlobals &kg,
                          const ShadingData &sd,
                          float &shade,
                          float &light,
                          float &cast_shadow,
                          float &form_shadow,
                          float &facing_light,
                          float &self_shadow)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  self_shadow = 0.0f;
  if (dot(light_dir, light_dir) < 1e-12f) {
    /* No direction: the light is off (Ensō Light Select with a hidden light). */
    shade = 0.0f;
    light = 0.0f;
    cast_shadow = 0.0f;
    form_shadow = 1.0f;
    facing_light = -1.0f;
    return;
  }
  float3 L = normalize(light_dir);
  float nl = dot(N, L);
  facing_light = nl;
  float w = saturate(wrap);
  float v = (nl + w) / (1.0f + w);
  light = enso_cel_step(terminator, v, softness, edge_px);
  form_shadow = 1.0f - light;
  float open_light = enso_cel_step(0.5f, saturate(mask), softness, edge_px);
  /* Shadows found on screen: follow the way to the light through the scene depth. What blocks
   * it is either the object itself or another one. */
  float by_other = 0.0f;
  float by_self = 0.0f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  if (shadow_distance > 0.0f && nl > 0.0f) {
    /* The engine's own screen ray-cast, the one behind the Raycast node: it also tells whether
     * the hit is this object or another one. */
    bool is_hit;
    bool self_hit;
    float hit_distance;
    float3 hit_position;
    float3 hit_normal;
    raycast_eval(kg,
                 sd,
                 sd.P + N * 0.005f,
                 L,
                 shadow_distance,
                 false,
                 is_hit,
                 self_hit,
                 hit_distance,
                 hit_position,
                 hit_normal);
    if (is_hit) {
      if (!self_hit) {
        by_other = 1.0f;
      }
      else if (hit_distance > 0.02f) {
        by_self = 1.0f;
      }
    }
  }
#endif
  cast_shadow = light * max(1.0f - open_light, by_other);
  self_shadow = light * by_self;
  shade = light * open_light * (1.0f - by_other) * (1.0f - by_self);
}

/* Ensō Stylized Highlight: a highlight that can be moved, stretched, split and squared. */
[[node]]
void node_enso_highlight(float3 light_dir,
                         float3 up,
                         float size,
                         float stretch_x,
                         float stretch_y,
                         float offset_x,
                         float offset_y,
                         float split,
                         float squareness,
                         float softness,
                         float edge_px,
                         float3 N,
                         [[resource_table]] KernelGlobals &kg,
                         ShadingData &sd,
                         float &highlight)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 Uw = enso_cel_normalize((obm.model * float4(up, 0.0f)).xyz, float3(0.0f, 0.0f, 1.0f));
  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 L = enso_cel_normalize(light_dir, V);
  float3 H = enso_cel_normalize(L + V, V);
  /* Frame on the surface: T runs sideways, B runs up. */
  float3 T = enso_cel_normalize(cross(Uw, N), float3(1.0f, 0.0f, 0.0f));
  float3 B = cross(N, T);
  float hx = dot(H, T) - offset_x;
  float hy = dot(H, B) - offset_y;
  hx = abs(abs(hx) - max(split, 0.0f) * step(1e-6f, split));
  hx /= max(stretch_x, 1e-3f);
  hy = abs(hy) / max(stretch_y, 1e-3f);
  float p = mix(2.0f, 8.0f, saturate(squareness));
  float d = pow(pow(hx, p) + pow(hy, p), 1.0f / p);
  float lit = enso_cel_step(0.0f, dot(N, L), 0.1f, edge_px);
  highlight = (1.0f - enso_cel_step(max(size, 0.0f), d, softness, edge_px)) * lit;
}

/* Ensō Light Rim: a rim on the edge of the form, on the side the light comes from. */
[[node]]
void node_enso_light_rim(float3 light_dir,
                         float width,
                         float light_side,
                         float softness,
                         float edge_px,
                         float3 N,
                         [[resource_table]] KernelGlobals &kg,
                         ShadingData &sd,
                         float &rim)
{
  N = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 L = enso_cel_normalize(light_dir, V);
  float f = 1.0f - saturate(dot(N, V));
  float r = enso_cel_step(1.0f - saturate(width), f, softness, edge_px);
  float gate = enso_cel_step(0.0f, dot(N, L), 0.2f, edge_px);
  rim = r * mix(1.0f, gate, saturate(light_side));
}

/* Ensō Eye Parallax: slides the iris texture so it looks set back inside the eye. */
[[node]]
void node_enso_eye_parallax(float3 vector,
                            float3 forward,
                            float depth,
                            [[resource_table]] KernelGlobals &kg,
                            ShadingData &sd,
                            float3 &out_vector)
{
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 Vo = enso_cel_normalize((obm.model_inverse * float4(V, 0.0f)).xyz, float3(0.0f, -1.0f, 0.0f));
  float3 F = enso_cel_normalize(forward, float3(0.0f, -1.0f, 0.0f));
  float vf = max(dot(Vo, F), 0.2f);
  float3 slide = (Vo - F * dot(Vo, F)) / vf;
  out_vector = vector - slide * depth;
}

/* Ensō Light Select: direction, colour and power of one chosen light object. */
[[node]]
void node_enso_light_select(float4x4 light_matrix,
                            float3 light_color,
                            float light_power,
                            float is_sun,
                            [[resource_table]] KernelGlobals &kg,
                            const ShadingData &sd,
                            float3 &direction,
                            float4 &color,
                            float &strength,
                            float3 &position,
                            float &distance,
                            float &shadow)
{
  shadow = 1.0f;
  direction = float3(0.0f, 0.0f, 1.0f);
  color = float4(light_color, 1.0f);
  strength = light_power;
  position = float3(0.0f);
  distance = 0.0f;
  if (light_matrix[3][3] == 0.0f || light_power <= 0.0f) {
    /* No light chosen, or it is hidden or switched off: no direction at all, which the Ensō
     * shading nodes read as "unlit". */
    direction = float3(0.0f);
    strength = 0.0f;
    return;
  }
  position = light_matrix[3].xyz;
  /* The light's real shadow at this point: 1 where it reaches, 0 where something blocks it. */
  enso_light_shadow(kg, sd, light_matrix, sd.P, shadow);
  float3 to_light = position - sd.P;
  distance = length(to_light);
  if (is_sun > 0.5f) {
    /* A sun shines along its own -Z, so the light comes from +Z. */
    direction = enso_cel_normalize(light_matrix[2].xyz, float3(0.0f, 0.0f, 1.0f));
  }
  else {
    direction = enso_cel_normalize(to_light, float3(0.0f, 0.0f, 1.0f));
  }
}

/* The active UV map as a plain vector. */
[[node]]
void node_enso_attribute_uv(float4 uv_attr, float3 &uv)
{
  uv = uv_attr.xyz;
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō hair strands.
 *
 * Anime hair shading needs to know, at every point of the hair, which strand it is on, how far
 * across that strand it is and how far from the root. Games paint or bake that into a hair map.
 * Here it comes from the mesh: "Make Hair Strand Data" writes a UV map named "Enso Hair Strand"
 * (U across the strand, V from root to tip) and a random value per strand ("enso_strand").
 * Without that data the active UV map is cut into a number of even strands along U. */
void enso_hair_strand(float4 strand_uv,
                      float4 strand_id,
                      float4 uv,
                      float strands,
                      float &across,
                      float &along,
                      float &id)
{
  if (strand_id.x > 0.0f) {
    across = saturate(strand_uv.x);
    along = saturate(strand_uv.y);
    id = strand_id.x;
  }
  else {
    float x = uv.x * max(floor(strands + 0.5f), 1.0f);
    across = fract(x);
    /* Roots at the top of the UV map. */
    along = saturate(1.0f - uv.y);
    id = 0.05f + 0.95f * fract(sin((floor(x) + 3.0f) * 12.9898f) * 43758.5453f);
  }
}

/* The hair map made on the fly: a ridge across every strand, high in the middle and zero at the
 * edges, with its own height per strand and finer ridges inside it. Added to the light, it pulls
 * the shadow edge into points that follow the strands. */
float enso_hair_strand_map(float across, float along, float id, float variation, float detail)
{
  float d = saturate(detail);
  /* The ridge wanders a little from side to side on its way down, like a lock of hair. */
  float x = saturate(across + 0.1f * d * sin(along * 9.0f + id * 40.0f) *
                                  (1.0f - abs(across * 2.0f - 1.0f)));
  float ridge = 1.0f - abs(x * 2.0f - 1.0f);
  float g = pow(ridge, 0.8f) * (1.0f - saturate(variation) * fract(id * 7.31f));
  float count = 2.0f + floor(fract(id * 13.7f) * 3.0f);
  float xf = x * count + fract(id * 5.3f);
  float fine = 1.0f - abs(fract(xf) * 2.0f - 1.0f);
  float fine_height = 0.4f + 0.6f * fract(sin((floor(xf) + id * 17.0f) * 12.9898f) * 43758.5453f);
  g = mix(g, g * (0.3f + 0.7f * fine * fine_height), d);
  /* And it is not the same height all the way: flame shapes instead of straight stripes. */
  g *= 1.0f - 0.35f * d * (0.5f + 0.5f * sin(along * 14.0f + id * 71.0f));
  return saturate(g);
}

/* Ensō Hair Shadow: the light on the hair with the strand map added, cut into light and shadow.
 * The shadow follows the light and its edge breaks into strand shaped points. */
[[node]]
void node_enso_hair_shadow(float map,
                           float3 light_dir,
                           float map_strength,
                           float neutral,
                           float bias,
                           float softness,
                           float variation,
                           float detail,
                           float strands,
                           float edge_px,
                           float3 N,
                           float4 strand_uv,
                           float4 strand_id,
                           float4 uv,
                           float use_map,
                           float &lit,
                           float &strand_map,
                           float &strand)
{
  float across;
  float along;
  float id;
  enso_hair_strand(strand_uv, strand_id, uv, strands, across, along, id);
  strand = id;
  strand_map = (use_map > 0.5f) ? map :
                                  enso_hair_strand_map(across, along, id, variation, detail);
  float3 L = enso_cel_normalize(light_dir, float3(0.0f, 0.0f, 1.0f));
  float3 Nn = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  float value = dot(Nn, L) + (strand_map - neutral) * map_strength;
  lit = (dot(light_dir, light_dir) > 1e-8f) ? enso_cel_step(bias, value, softness, edge_px) :
                                               0.0f;
}

float enso_hair_hash(float x)
{
  return fract(sin(x * 12.9898f) * 43758.5453f);
}

/* Ensō Hair Highlight: the highlight of anime hair the way it is painted in a hair map, made on
 * the fly. A few strands carry a mark. A mark is tall and thin, runs along its strand and has a
 * jagged top and bottom, like a small bolt of lightning. The marks stay where they are on the
 * hair; a ring around the reflection of the light decides how bright they are, so they come and
 * go as the light and the camera move. */
[[node]]
void node_enso_hair_highlight(float map,
                              float3 light_dir,
                              float position,
                              float length_in,
                              float jagged,
                              float points,
                              float coverage,
                              float stroke,
                              float ring_size,
                              float ring_softness,
                              float intensity,
                              float view_shift,
                              float along_strand,
                              float3 center,
                              float strands,
                              float softness,
                              float edge_px,
                              float3 N,
                              float4 strand_uv,
                              float4 strand_id,
                              float4 uv,
                              float4 center_prop,
                              float use_map,
                              [[resource_table]] KernelGlobals &kg,
                              ShadingData &sd,
                              float &highlight,
                              float &marks,
                              float &strand)
{
  float across;
  float along;
  float id;
  enso_hair_strand(strand_uv, strand_id, uv, strands, across, along, id);
  strand = id;
  ObjectMatrices obm = kg.object_matrices_get(sd);
  /* Where on the hair the marks sit: at a height on the head measured down from the top (so
   * they line up around the head whatever the length of each strand), or at a distance from
   * the root of each strand. The height needs the head centre: the node's Center, or the one
   * Make Hair Strand Data stored on the object. */
  float t = along;
  if (dot(center, center) > 0.0f || center_prop.w > 0.5f) {
    float3 c = (dot(center, center) > 0.0f) ? center : center_prop.xyz;
    float3 Po = (obm.model_inverse * float4(sd.P, 1.0f)).xyz;
    float3 up_dir = enso_cel_normalize(Po - c, float3(0.0f, 0.0f, 1.0f));
    t = mix((1.0f - up_dir.z) * 0.5f, along, saturate(along_strand));
  }
  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 L = enso_cel_normalize(light_dir, float3(0.0f, 0.0f, 1.0f));
  float3 Nn = enso_cel_normalize(N, float3(0.0f, 0.0f, 1.0f));
  /* Looking from above moves the marks down the hair a little, like a reflection. */
  float place = position + view_shift * 0.5f * V.z;
  float len = max(length_in, 0.0f);
  float jag = saturate(jagged);

  /* The mark of this strand: its top and bottom edge are broken lines through a few points
   * across the strand, each point with its own height and its own length. Both ends close to
   * a point. */
  float xs = (across - 0.5f) / max(stroke, 1e-3f) + 0.5f;
  float mark = 0.0f;
  if (xs > 0.0f && xs < 1.0f) {
    float k = max(floor(points + 0.5f), 1.0f);
    float xk = xs * k;
    float j = floor(xk);
    float f = xk - j;
    float e0 = max(1.0f - abs(2.0f * j / k - 1.0f), 0.0f);
    float e1 = max(1.0f - abs(2.0f * (j + 1.0f) / k - 1.0f), 0.0f);
    float c0 = (enso_hair_hash(id * 97.0f + j * 13.1f) - 0.5f) * jag * len * 1.4f;
    float c1 = (enso_hair_hash(id * 97.0f + (j + 1.0f) * 13.1f) - 0.5f) * jag * len * 1.4f;
    float l0 = len * e0 * mix(1.0f, 0.25f + 1.1f * enso_hair_hash(id * 57.0f + j * 7.7f), jag);
    float l1 = len * e1 *
               mix(1.0f, 0.25f + 1.1f * enso_hair_hash(id * 57.0f + (j + 1.0f) * 7.7f), jag);
    float own = (fract(id * 37.3f) - 0.5f) * jag * len * 0.8f;
    float inside = mix(l0, l1, f) * 0.5f - abs(t - (place + own + mix(c0, c1, f)));
    mark = enso_cel_step(0.0f, inside, softness * max(len, 1e-4f), edge_px);
  }
  marks = mark * step(1.0f - saturate(coverage), fract(id * 91.7f));
  /* The ring: full strength where the surface mirrors the light into the camera, fading out
   * around it. */
  float3 H = enso_cel_normalize(L + V, Nn);
  float edge = 1.0f - saturate(ring_size);
  float ring = (dot(light_dir, light_dir) > 1e-8f) ?
                   smoothstep(edge - max(ring_softness, 1e-4f), edge, dot(Nn, H)) :
                   0.0f;
  highlight = ((use_map > 0.5f) ? saturate(map) : marks) * ring * max(intensity, 0.0f);
}

/* Ensō passes: running value of each pass for the pixel being shaded. A node inside the light
 * loop runs once per light, so the pass keeps the highest value seen. */
float4 g_enso_pass_a = float4(0.0f);
float4 g_enso_pass_b = float4(0.0f);
float4 g_enso_pass_c = float4(0.0f);
float4 g_enso_pass_d = float4(0.0f);
float4 g_enso_pass_e = float4(0.0f);
float4 g_enso_pass_f = float4(0.0f);

[[node]]
void node_enso_pass(float value,
                    float slot,
                    float hash,
                    [[resource_table]] KernelGlobals &kg,
                    const ShadingData &sd,
                    Closure & /*dummy*/)
{
  int i = int(slot);
  int c = i & 3;
  float acc = value;
  if (i < 4) {
    acc = max(g_enso_pass_a[c], value);
    g_enso_pass_a[c] = acc;
  }
  else if (i < 8) {
    acc = max(g_enso_pass_b[c], value);
    g_enso_pass_b[c] = acc;
  }
  else if (i < 12) {
    acc = max(g_enso_pass_c[c], value);
    g_enso_pass_c[c] = acc;
  }
  else if (i < 16) {
    acc = max(g_enso_pass_d[c], value);
    g_enso_pass_d[c] = acc;
  }
  else if (i < 20) {
    acc = max(g_enso_pass_e[c], value);
    g_enso_pass_e[c] = acc;
  }
  else {
    acc = max(g_enso_pass_f[c], value);
    g_enso_pass_f[c] = acc;
  }
  output_aov(kg,
             int2(sd.frag_co.xy),
             float4(acc),
             acc,
             floatBitsToUint(hash),
             sd.holdout,
             kg.object_infos_get(sd).flag);
}

/* Ensō Part Tags: the colour passes through; the tags only go to the passes. */
[[node]]
void node_enso_part_tags(float4 color,
                         float /*face*/,
                         float /*hair*/,
                         float /*eye*/,
                         float /*skin*/,
                         float /*cloth*/,
                         float4 &out_color)
{
  out_color = color;
}
