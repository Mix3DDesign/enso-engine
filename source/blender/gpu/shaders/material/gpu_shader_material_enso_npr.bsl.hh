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

#include "gpu_shader_codegen_lib.glsl"
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

/* ------------------------------------------------------------------------------------------ */
/* Curvature shared by Ensō Curvature and Ensō Screen Curvature.
 *
 * Looks at the pre-pass normals on two rings around the pixel and measures how much they lean
 * away from the centre (a bump or outer edge) or toward it (a dent or crease), per meter on the
 * surface. The result is that bend times the radius, so an edge gets a band as wide as the
 * radius whose strength follows the angle of the edge, and it does not change with the camera
 * distance. Other objects and far depth steps are left out; another object touching the
 * surface inside the radius is reported as contact. */
void enso_curvature_core([[resource_table]] KernelGlobals &kg,
                         const ShadingData &sd,
                         float radius,
                         float depth_threshold,
                         float blur,
                         float &curvature,
                         float &contact)
{
  curvature = 0.0f;
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
  float zv = enso_npr_view_z(view, sd.P);
  float ppm = enso_npr_px_per_m(view, sd.P, size);
  if (zv < 1e-6f || ppm < 1e-6f) {
    return;
  }
  /* Pre-pass depth unit, so depth steps below are in meters. */
  float unit = d0 / zv;
  /* Never less than a few pixels: one pixel only sees the facets of the mesh. */
  float r_m = max(max(radius, 0.0f), 3.0f / ppm);
  float r_px = r_m * ppm;
  /* The taps are laid out on the surface around the point (its tangent plane), in meters, and
   * then looked up on screen. The band around an edge is then as wide as the radius on the
   * object, whatever the distance and the angle the face is seen from. */
  float3 t1 = cross(N0, (abs(N0.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f));
  t1 = normalize(t1);
  float3 t2 = cross(N0, t1);
  /* Direction to the camera. */
  bool is_persp = view.winmat[3][3] == 0.0f;
  float3 V = is_persp ? normalize(view.viewinv[3].xyz - sd.P) : normalize(view.viewinv[2].xyz);
  /* A tap that lands past the outline of the object means the surface turned away there: the
   * edge is on the outline. That only counts on faces that look at the camera. On the rim of a
   * round shape the surface is seen edge on, and there it says nothing. */
  float outline_weight = smoothstep(0.12f, 0.45f, dot(N0, V));
  /* Depth the surface covers inside one pixel. A face seen at a low angle covers a lot, and
   * the depth of a tap can be that far off without being another surface. */
  float3 vz = float3(view.viewmat[0][2], view.viewmat[1][2], view.viewmat[2][2]);
  float pixel_depth = (abs(dot(vz, gpu_dfdx(sd.P))) + abs(dot(vz, gpu_dfdy(sd.P)))) * 2.0f;
  float jit;
  enso_pixel_jitter(kg, px, jit);
  /* Rings of eight directions. A wide radius gets more rings, and the rings move in and out
   * with every render sample, so the band around an edge is one clean gradient. */
  const int directions = 8;
  float sum = 0.0f;
  float weight = 0.0f;
  /* Blur: the same measure is taken around four more points next to the point and averaged.
   * The normals of a mesh change in steps from face to face, and one measure shows those
   * steps as dots or lines when the radius is close to the size of a face. */
  bool blurred = blur > 0.001f;
  int centers = blurred ? 5 : 1;
  int rings = blurred ? 3 : ((r_px > 24.0f) ? 6 : 4);
  float spread = r_m * saturate(blur) * 0.7f;
  float jit_r = fract(jit * 7.31f + 0.137f);
  for (int center = 0; center < centers; center++) {
    float3 Pc = sd.P;
    if (center > 0) {
      float ca = (float(center) + jit * 0.5f) * 1.5707963f;
      Pc += (t1 * cos(ca) + t2 * sin(ca)) * spread;
    }
    for (int ring = 0; ring < rings; ring++) {
      float f = (float(rings - ring) - jit_r) / float(rings);
      float rho = max(r_m * f, 1.0f / ppm);
      /* How far the visible surface may be from the tap and still be the same surface. */
      float reach = rho * 2.0f + pixel_depth + max(depth_threshold, 0.0f) * 0.1f;
      for (int i = 0; i < directions * 2; i++) {
        float a = (float(i) + 0.29f * float(ring) + jit) * (3.14159265f / float(directions));
        float3 Dt = t1 * cos(a) + t2 * sin(a);
        float3 W = Pc + Dt * rho;
        bool on_screen;
        float2 q = enso_npr_world_to_px(view, W, size, on_screen);
        if (!on_screen || any(lessThan(q, float2(0.0f))) || any(greaterThanEqual(q, size))) {
          continue;
        }
        bool vq;
        float3 Nq;
        float dq;
        uint idq;
        enso_prepass_fetch(kg, q, vq, Nq, dq, idq);
        bool past_outline = false;
        if (!vq || dq > 1e9f) {
          /* Nothing there but the background. */
          past_outline = true;
        }
        else {
          /* Positive: what is seen there is behind the tap. */
          float gap = dq / unit - enso_npr_view_z(view, W);
          if (idq != id0) {
            if (abs(gap) < rho * 1.5f) {
              contact = max(contact, 1.0f - f * 0.5f);
            }
            if (gap <= reach) {
              continue;
            }
            past_outline = true;
          }
          else if (gap > reach) {
            past_outline = true;
          }
          else if (gap < -reach) {
            /* Another part of the object in front. */
            continue;
          }
        }
        if (past_outline) {
          /* Counts as a right-angle edge. */
          sum += outline_weight / rho;
          weight += outline_weight;
        }
        else {
          sum += dot(Nq - N0, Dt) / rho;
          weight += 1.0f;
        }
      }
    }
  }
  curvature = (weight > 0.0f) ? (sum / weight) * r_m : 0.0f;
#endif
}

/* Turns the raw bend into a clean 0..1 mask: small bends (the facets of a low-poly mesh) are
 * dropped by the threshold, the rest is scaled by the contrast. */
float enso_curvature_shape(float value, float threshold, float contrast)
{
  /* A soft shoulder instead of a hard clip: strong curvature still reaches white, but the way
   * there is a gradient, so a wide radius gives form shading and not two flat tones. */
  float x = max(value - max(threshold, 0.0f), 0.0f) * max(contrast, 0.0f);
  return 1.0f - exp(-1.6f * x);
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Screen Curvature: edges and cavities from the pre-pass normals around the pixel. */
[[node]]
void node_enso_screen_curvature(float radius,
                                float strength,
                                float threshold,
                                float depth_threshold,
                                float blur,
                                [[resource_table]] KernelGlobals &kg,
                                const ShadingData &sd,
                                float &convex,
                                float &concave,
                                float &signed_curvature)
{
  float c;
  float contact;
  enso_curvature_core(kg, sd, radius, depth_threshold, blur, c, contact);
  convex = enso_curvature_shape(c, threshold, strength);
  concave = enso_curvature_shape(-c, threshold, strength);
  signed_curvature = convex - concave;
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Reflection */

/* What the surface would reflect, found on screen: the view ray is mirrored on the surface and
 * followed through the scene's depth. It gives a mask of the reflected shapes and a flat colour
 * per reflected object, to paint fake reflections on floors and water. */
[[node]]
void node_enso_reflection(float3 N,
                          float distance,
                          float thickness,
                          float fade,
                          float reflect_self,
                          float stretch,
                          [[resource_table]] KernelGlobals &kg,
                          ShadingData &sd,
                          float &mask,
                          float4 &color,
                          float &hit_distance)
{
  mask = 0.0f;
  color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  hit_distance = 1.0f;
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
  float zv = enso_npr_view_z(view, sd.P);
  if (zv < 1e-6f) {
    return;
  }
  float unit = d0 / zv;
  float3 V = coordinate_impl(kg, sd, sd.P, sd.N).incoming;
  float3 Nn = normalize(N);
  /* Stretch: a real reflection shrinks under the object as the camera rises. Lowering the
   * camera only for the reflection keeps it long and readable from any angle. */
  float up = dot(V, Nn);
  V = normalize(V - Nn * (up * 0.92f * saturate(stretch)));
  float3 R = reflect(-V, Nn);
  float jit;
  enso_pixel_jitter(kg, px, jit);
  float reach = max(distance, 1e-3f);
  const int steps = 64;
  for (int i = 0; i < steps; i++) {
    float t = (float(i) + 0.5f + jit) / float(steps);
    /* Short steps near the surface, longer ones far away. */
    float s = t * (0.25f + 0.75f * t);
    float3 W = sd.P + Nn * 0.003f + R * (s * reach);
    bool ok;
    float2 q = enso_npr_world_to_px(view, W, size, ok);
    if (!ok || any(lessThan(q, float2(0.0f))) || any(greaterThanEqual(q, size))) {
      break;
    }
    bool vq;
    float3 Nq;
    float dq;
    uint idq;
    enso_prepass_fetch(kg, q, vq, Nq, dq, idq);
    if (!vq || dq > 1e9f) {
      continue;
    }
    if (idq == id0 && reflect_self < 0.5f) {
      continue;
    }
    float behind = enso_npr_view_z(view, W) - dq / unit;
    float step_len = reach * (0.25f + 1.5f * t) / float(steps);
    if (behind > 0.0f && behind < max(thickness, 1e-3f) + step_len) {
      float border = min(min(q.x, size.x - q.x), min(q.y, size.y - q.y)) / (size.x * 0.06f);
      float far_fade = 1.0f - smoothstep(1.0f - saturate(fade), 1.0f, s);
      mask = saturate(border) * far_fade;
      hit_distance = s;
      enso_object_color(kg, idq, color);
      color.a = 1.0f;
      break;
    }
  }
#endif
}

/* ------------------------------------------------------------------------------------------ */
/* Ensō Blur */

/* Blur of anything: the nodes plugged into Value are evaluated 24 times with their texture
 * coordinates moved over a disc inside the texture (enso_blur_branch in the codegen library), and this
 * node returns the average. The setup node runs first and gives the disc its size. */
[[node]]
void node_enso_blur_setup(float radius,
                          [[resource_table]] KernelGlobals &kg,
                          const ShadingData &sd,
                          float &ready)
{
  /* Radius is in percent of the texture: 1 is a hundredth of a UV tile. */
  g_enso_blur_radius = clamp(radius, 0.0f, 100.0f) * 0.01f;
  g_enso_blur_rotation = 0.0f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  float jit;
  enso_pixel_jitter(kg, sd.frag_co.xy, jit);
  g_enso_blur_rotation = jit * 6.2831853f;
#endif
  ready = 1.0f;
}

[[node]]
void node_enso_blur(float /*ready*/, float4 blurred, float4 &result)
{
  result = blurred;
}
