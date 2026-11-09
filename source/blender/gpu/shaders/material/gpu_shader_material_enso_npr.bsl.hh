/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō NPR nodes built on the EEVEE pre-pass (normal, depth in meters, object id per pixel):
 *
 *   Ensō Screen Rim    depth-offset rim light, anime style
 *   Ensō Line Detect   silhouette, crease, object border and contact lines as separate masks
 *   Ensō Hatching      manga halftone dots, lines and crosshatch driven by a shade value,
 *                      anchored to the object so the pattern does not swim
 */

#pragma once

#include "gpu_shader_material_interface.bsl.hh"

/* Pixel position of a world point. */
float2 enso_npr_world_to_px(const ViewMatrices &view, float3 W, float2 size, bool &ok)
{
  float4 h = view.winmat * (view.viewmat * float4(W, 1.0f));
  ok = h.w > 1e-6f;
  float2 ndc = h.xy / max(h.w, 1e-6f);
  return (ndc * 0.5f + 0.5f) * size;
}

/* View depth (meters) of a world point. */
float enso_npr_view_z(const ViewMatrices &view, float3 W)
{
  return abs((view.viewmat * float4(W, 1.0f)).z);
}

/* Pixels per meter at the point, measured sideways on the screen. */
float enso_npr_px_per_m(const ViewMatrices &view, float3 P, float2 size)
{
  float3 right = normalize(view.viewinv[0].xyz);
  bool ok0;
  bool ok1;
  float2 a = enso_npr_world_to_px(view, P, size, ok0);
  float2 b = enso_npr_world_to_px(view, P + right * 0.01f, size, ok1);
  return (ok0 && ok1) ? length(b - a) / 0.01f : 0.0f;
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Screen Rim */

[[node]]
void node_enso_screen_rim(float width,
                          float threshold,
                          float softness,
                          float3 direction,
                          [[resource_table]] KernelGlobals &kg,
                          const ShadingData &sd,
                          float &rim)
{
  rim = 0.0f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float2 size;
  enso_prepass_size(kg, size);
  if (size.x <= 1.0f) {
    return;
  }
  const ViewMatrices view = kg.view_matrices_get(sd);
  float3 P = sd.P;
  bool persp = view.winmat[3][3] == 0.0f;
  float3 V = persp ? normalize(view.viewinv[3].xyz - P) : normalize(view.viewinv[2].xyz);
  float3 N = normalize(float3(sd.N));
  /* Push direction: towards the light when one is given, else outward along the normal. */
  float3 d = (dot(direction, direction) > 1e-8f) ? normalize(direction) : N;
  d = d - V * dot(d, V);
  if (dot(d, d) < 1e-8f) {
    return;
  }
  d = normalize(d);
  float zv = enso_npr_view_z(view, P);
  float jit;
  enso_pixel_jitter(kg, sd.frag_co.xy, jit);
  float acc = 0.0f;
  const int taps = 3;
  for (int i = 0; i < taps; i++) {
    float t = mix(0.55f, 1.0f, (float(i) + jit) / float(taps));
    float3 W = P + d * (max(width, 1e-5f) * t);
    bool ok;
    float2 q = enso_npr_world_to_px(view, W, size, ok);
    if (!ok || any(lessThan(q, float2(0.0f))) || any(greaterThanEqual(q, size))) {
      acc += 1.0f;
      continue;
    }
    bool vq;
    float3 Nq;
    float dq;
    uint idq;
    enso_prepass_fetch(kg, q, vq, Nq, dq, idq);
    if (!vq || dq > 1e9f) {
      acc += 1.0f;
      continue;
    }
    /* Something there is clearly farther than this point: the surface ends on that side. */
    float gap = dq - max(zv, enso_npr_view_z(view, W));
    float s = max(softness, 1e-4f) * max(threshold, 1e-4f);
    acc += smoothstep(threshold - s, threshold + s, gap);
  }
  rim = acc / float(taps);
#endif
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Line Detect */

[[node]]
void node_enso_line_detect(float width,
                           float crease_angle,
                           float depth_threshold,
                           [[resource_table]] KernelGlobals &kg,
                           const ShadingData &sd,
                           float &line,
                           float &silhouette,
                           float &crease,
                           float &border,
                           float &contact)
{
  line = 0.0f;
  silhouette = 0.0f;
  crease = 0.0f;
  border = 0.0f;
  contact = 0.0f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float2 size;
  enso_prepass_size(kg, size);
  float2 px = sd.frag_co.xy;
  bool v0;
  float3 N0;
  float d0;
  uint id0;
  enso_prepass_fetch(kg, px, v0, N0, d0, id0);
  if (!v0 || d0 > 1e9f || size.x <= 1.0f) {
    return;
  }
  const ViewMatrices view = kg.view_matrices_get(sd);
  float radius = clamp(max(width, 0.0f) * 0.5f * enso_npr_px_per_m(view, sd.P, size), 0.75f, 96.0f);
  float cos_crease = cos(clamp(crease_angle, 0.0f, 3.14159f));
  /* A depth step counts when it is bigger than the threshold, or than what the face's own
   * slope gives across the radius (so grazing floors are not outlined). */
  float3 dPdx = gpu_dfdx(sd.P);
  float3 dPdy = gpu_dfdy(sd.P);
  float3 vz = float3(view.viewmat[0][2], view.viewmat[1][2], view.viewmat[2][2]);
  float slope = (abs(dot(vz, dPdx)) + abs(dot(vz, dPdy))) * radius * 1.5f;
  float step_m = max(depth_threshold, 1e-4f) + slope;
  float jit;
  enso_pixel_jitter(kg, px, jit);
  const int taps = 8;
  for (int ring = 0; ring < 2; ring++) {
    float r = (ring == 0) ? radius : radius * 0.5f;
    for (int i = 0; i < taps; i++) {
      float a = (float(i) + 0.5f * float(ring) + jit) * (6.2831853f / float(taps));
      float2 q = px + float2(cos(a), sin(a)) * r;
      if (any(lessThan(q, float2(0.0f))) || any(greaterThanEqual(q, size))) {
        continue;
      }
      bool vq;
      float3 Nq;
      float dq;
      uint idq;
      enso_prepass_fetch(kg, q, vq, Nq, dq, idq);
      if (!vq || dq > 1e9f) {
        silhouette = 1.0f;
        continue;
      }
      float gap = dq - d0;
      if (idq != id0) {
        border = 1.0f;
        if (abs(gap) < step_m) {
          contact = 1.0f;
        }
        else if (gap > 0.0f) {
          silhouette = 1.0f;
        }
        continue;
      }
      if (gap > step_m && dot(N0, Nq) < 0.97f) {
        silhouette = 1.0f;
      }
      if (dot(N0, Nq) < cos_crease) {
        crease = 1.0f;
      }
    }
  }
  line = max(max(silhouette, crease), max(border, contact));
#endif
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Hatching */

/* Line pattern with screen-space anti-aliasing: 1 inside a stripe of the given coverage. */
float enso_hatch_stripes(float u, float coverage, float aa)
{
  float f = abs(fract(u) - 0.5f);
  float half_w = coverage * 0.5f;
  return 1.0f - smoothstep(half_w - aa, half_w + aa, f);
}

[[node]]
void node_enso_hatching(float shade,
                        float scale,
                        float angle,
                        float softness,
                        [[resource_table]] KernelGlobals &kg,
                        const ShadingData &sd,
                        float &dots,
                        float &lines,
                        float &crosshatch)
{
  dots = 0.0f;
  lines = 0.0f;
  crosshatch = 0.0f;
  float dark = saturate(1.0f - shade);
  float2 uv = float2(0.0f);
  float aa = 0.05f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  const ViewMatrices view = kg.view_matrices_get(sd);
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 origin = obm.model[3].xyz;
  /* Screen-space pattern pinned to the object: it moves with the object's center and scales
   * with its distance, so it stays the same size on the object and never swims across it. */
  float2 size = float2(1.0f);
  enso_prepass_size(kg, size);
  if (size.x <= 1.0f) {
    size = float2(1920.0f, 1080.0f);
  }
  bool ok;
  float2 o_px = enso_npr_world_to_px(view, origin, size, ok);
  float ppm = max(enso_npr_px_per_m(view, origin, size), 1e-3f);
  uv = (sd.frag_co.xy - o_px) / ppm * max(scale, 0.01f);
  float2 duv = abs(gpu_dfdx(uv)) + abs(gpu_dfdy(uv));
  aa = max(max(duv.x, duv.y), 1e-4f) * (0.75f + 2.0f * softness);
#endif
  float c = cos(angle);
  float s = sin(angle);
  float2 ruv = float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
  /* Halftone dots: the darker, the bigger. */
  float2 cell = fract(ruv) - 0.5f;
  float r = sqrt(dark) * 0.72f;
  dots = (dark > 0.002f) ? 1.0f - smoothstep(r - aa, r + aa, length(cell)) : 0.0f;
  /* Lines: the darker, the thicker. */
  lines = (dark > 0.002f) ? enso_hatch_stripes(ruv.y, dark, aa) : 0.0f;
  /* Crosshatch: a second set at right angles comes in for the darker half. */
  float l1 = enso_hatch_stripes(ruv.y, min(dark * 1.6f, 1.0f) * 0.55f, aa);
  float d2 = saturate((dark - 0.45f) / 0.55f);
  float l2 = (d2 > 0.002f) ? enso_hatch_stripes(ruv.x, d2 * 0.55f, aa) : 0.0f;
  crosshatch = (dark > 0.002f) ? max(l1, l2) : 0.0f;
}
