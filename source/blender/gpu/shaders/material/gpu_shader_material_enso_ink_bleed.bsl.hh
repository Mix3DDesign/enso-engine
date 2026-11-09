/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō Ink Bleed node: ink lines on the creases and outline of an object, with ink that bleeds
 * down from the lines in soft rounded drips.
 *
 * Lines: found in the EEVEE pre-pass (normal changes and depth steps) within a width given in
 * meters, so they keep their size on the object; the sample ring turns every sample and the
 * anti-aliasing blends it smooth.
 * Drips: measured on the object. The "Ensō Edge Data" modifier (added automatically) gives every
 * point the place on the edge above it and how far below it is, in the rest shape, so the drips
 * are painted on the surface: they stay in place when the camera or object moves. */

#pragma once

#include "gpu_shader_common_hash.bsl.hh"
#include "gpu_shader_material_interface.bsl.hh"
#include "gpu_shader_material_noise.bsl.hh"

/* Line strength at a pixel: 8 taps on a ring of the given radius. */
float enso_ink_line([[resource_table]] KernelGlobals &kg,
                    float2 px,
                    float radius,
                    float crease,
                    float outline,
                    float rot,
                    uint want_id,
                    bool check_id,
                    int taps)
{
  bool v0;
  float3 N0;
  float d0;
  uint id0;
  enso_prepass_fetch(kg, px, v0, N0, d0, id0);
  if (!v0 || d0 > 1e9f || (check_id && id0 != want_id)) {
    return 0.0f;
  }
  float c = 0.0f;
  float o = 0.0f;
  /* Two fixed rings (full and half radius): solid, evenly wide lines with no gaps. */
  float step_a = 6.2831853f / float(taps);
  for (int ring = 0; ring < 2; ring++) {
    float r = (ring == 0) ? radius : radius * 0.5f;
    for (int i = 0; i < taps; i++) {
      float a = rot + (float(i) + 0.5f * float(ring)) * step_a;
      float2 q = px + float2(cos(a), sin(a)) * r;
      bool vq;
      float3 Nq;
      float dq;
      uint idq;
      enso_prepass_fetch(kg, q, vq, Nq, dq, idq);
      if (!vq) {
        continue;
      }
      c = max(c, 1.0f - dot(N0, Nq));
      /* The neighbor is clearly farther: we are on the near side of a silhouette or border. */
      /* An outer edge: another object behind, or a clear depth step on this object where the
       * surface also turns (a plane seen at a grazing angle is not an edge). */
      if (idq != id0 && dq > d0 * 1.02f) {
        o = 1.0f;
      }
      else if (idq == id0 && dq > d0 * 1.08f && dot(N0, Nq) < 0.97f) {
        o = 1.0f;
      }
    }
  }
  float t = mix(0.6f, 0.012f, crease);
  float cv = (crease > 0.0f) ? smoothstep(t, t * 1.25f + 0.005f, c) : 0.0f;
  return max(cv, o * outline);
}

/* Drip mask at depth d below an edge point C (rest space): a thin wavy rim of ink along the
 * edge, and separate runs hanging from it. Each run wanders and changes width as it falls and
 * thins out to a point, like ink running down a wall (no round drops). tang is the horizontal
 * direction along the edge. */
float enso_ink_drip(float3 C,
                    float d,
                    float3 R,
                    float2 tang,
                    float freq,
                    float3 sv,
                    float variation,
                    float rain,
                    float rain_length,
                    float smudge,
                    float m_per_px,
                    float line_width)
{
  float px_w = m_per_px * 0.75f;
  /* Edges stay crisp at any setting: Smudge makes them wetter and more wandering instead. */
  float soft = max(rain_length * 0.002f, m_per_px * 0.6f) + 1e-4f;
  /* Runs are about as thick as the ink line, however long they get. */
  float base_w = max(line_width, 0.002f) * 0.6f;
  if (d < -2.0f * soft - 0.002f) {
    return 0.0f;
  }
  float wobble = (noise(R * freq * 4.0f + sv + 5.0f) - 0.5f) * (0.3f + smudge) * base_w * 2.0f;
  float dd = d + wobble;
  /* The rim: ink pooled along the edge. */
  float rim = base_w * (1.0f + 2.0f * rain) * (0.6f + 0.8f * noise(C * freq * 1.7f + sv + 11.0f));
  float m = 1.0f - smoothstep(rim - soft, rim + soft, dd);

  float2 perp = float2(-tang.y, tang.x);
  float cell_size = 1.0f / max(freq, 1e-4f);
  float3 q = C * freq + sv;
  float3 cell = floor(q);
  for (int z = -1; z <= 1; z++) {
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        float3 c = cell + float3(float(x), float(y), float(z));
        float3 hh = hash_vec3_to_vec3(c);
        /* Rain sets how many runs there are. */
        if (hh.x > rain) {
          continue;
        }
        float3 hh2 = hash_vec3_to_vec3(c + 17.3f);
        float3 center = (c + hh2 - sv) / freq;
        float2 rel = C.xy - center.xy;
        /* Only runs that start on this stretch of the edge. */
        if (abs(dot(rel, perp)) > cell_size * 0.5f || abs(C.z - center.z) > cell_size * 0.5f) {
          continue;
        }
        float s_along = dot(rel, tang);
        /* A few long runs and many short ones; Drip Variation 0 makes them all as long. */
        float len_k = mix(1.0f, 0.15f + 1.7f * pow(hh.z, 2.5f), variation);
        float Ld = rain_length * len_k;
        float w = max(base_w * (0.6f + 0.8f * hh.y) * (1.0f + 0.4f * smudge), px_w);
        float reach = w * (6.0f + 6.0f * smudge) + Ld * 0.12f;
        if (abs(s_along) > reach || dd > Ld + soft) {
          continue;
        }
        float t = saturate(dd / max(Ld, 1e-5f));
        float grip = saturate(dd / (w * 5.0f));
        /* The run wanders sideways as it falls (more with Smudge), with a slight slant of its own. */
        float wander = (noise(float3(dd / max(w * 14.0f, 1e-4f), hh.x * 13.0f, hh2.y * 7.0f)) - 0.5f) *
                           2.0f * w * (1.2f + 3.5f * smudge) +
                       (noise(float3(dd / max(w * 40.0f, 1e-4f), hh2.x * 5.0f, 9.0f)) - 0.5f) *
                           2.0f * w * (1.5f + 3.0f * smudge);
        float slant = dd * (hh2.z - 0.5f) * (0.06f + 0.1f * smudge);
        float h = abs(s_along - (wander + slant) * grip);
        /* Its width changes along the way and it thins out to a point at the end. */
        float uneven = 1.0f + (0.35f + 0.45f * smudge) *
                                  (noise(float3(dd / max(w * 5.0f, 1e-4f), hh.y * 31.0f, hh.z * 17.0f)) - 0.5f) * 2.0f;
        float taper = sqrt(saturate((Ld - dd) / max(min(Ld, w * 10.0f), 1e-5f)));
        float width = max(w * (1.0f - 0.3f * t) * uneven, px_w) * taper +
                      w * 1.6f * exp(-max(dd, 0.0f) / (w * 1.2f));
        float run = 1.0f - smoothstep(width - soft, width + soft, h);
        m = max(m, run * step(0.0f, Ld - dd + soft));
      }
    }
  }
  return m * smoothstep(-2.0f * soft - 0.002f, 0.0f, d);
}

[[node]]
void node_enso_ink_bleed(float width,
                         float crease,
                         float outline,
                         float detail,
                         float variation,
                         float rain,
                         float rain_length,
                         float drip_variation,
                         float smudge,
                         float seed,
                         float4 attr_anchor,
                         float4 attr_drop,
                         float4 attr_rest,
                         float4 attr_valid,
                         float4 attr_face_a,
                         float4 attr_face_b,
                         float4 attr_face_has,
                         [[resource_table]] KernelGlobals &kg,
                         const ShadingData &sd,
                         float &mask,
                         float &line,
                         float &leak)
{
  mask = 0.0f;
  line = 0.0f;
  leak = 0.0f;
  float3 sv = float3(seed * 13.17f, seed * 7.31f, seed * 3.77f);
  float freq = max(detail, 0.001f);
  bool has_data = attr_valid.x > 0.5f;
  ObjectMatrices obm = kg.object_matrices_get(sd);
  float3 R = has_data ? attr_rest.xyz : (obm.model_inverse * float4(sd.P, 1.0f)).xyz;

  /* Lines: even width on the object; Variation makes them hand-drawn and uneven. */
  if (crease > 0.0f || outline > 0.0f) {
    const ViewMatrices view = kg.view_matrices_get(sd);
    float2 size;
    enso_prepass_size(kg, size);
    float2 px = sd.frag_co.xy;
    bool v0;
    float3 N0;
    float d0;
    uint id0;
    enso_prepass_fetch(kg, px, v0, N0, d0, id0);
    if (v0 && size.x > 1.0f) {
      float3 cam_right = normalize(view.viewinv[0].xyz);
      float4 hp0 = view.winmat * (view.viewmat * float4(sd.P, 1.0f));
      float4 hp1 = view.winmat * (view.viewmat * float4(sd.P + cam_right * 0.01f, 1.0f));
      float px_per_m = (hp0.w > 0.0f && hp1.w > 0.0f) ?
                           length((hp1.xy / hp1.w - hp0.xy / hp0.w) * 0.5f * size) / 0.01f :
                           0.0f;
      float vary = 1.0f;
      if (variation > 0.0f) {
        vary = mix(1.0f, 0.35f + 1.3f * noise(R * freq * 0.5f + sv + 31.7f), variation);
      }
      float radius = clamp(width * px_per_m * vary, 0.75f, 160.0f);
      float jit;
      enso_pixel_jitter(kg, px, jit);
      line = enso_ink_line(kg, px, radius, crease, outline, jit * 6.2831853f, id0, false, 8);
    }
  }

  /* Drips running down from the edges above. */
  float m_per_px = 0.0f;
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  m_per_px = max(length(gpu_dfdx(R)), length(gpu_dfdy(R)));
#endif
  /* Horizontal direction along the surface (the way the edge above runs). */
  float2 surf_tang = float2(1.0f, 0.0f);
  {
    ObjectMatrices om = kg.object_matrices_get(sd);
    float3 Nl = normalize((float4(float3(sd.Ng), 0.0f) * om.model).xyz);
    float2 tn = float2(-Nl.y, Nl.x);
    if (dot(tn, tn) > 1e-8f) {
      surf_tang = normalize(tn);
    }
  }
  if (has_data && rain > 0.0f && rain_length > 0.0f) {
    float3 Ng = normalize((float4(float3(sd.Ng), 0.0f) * obm.model).xyz);
    if (Ng.z < 0.7f) {
      if (attr_face_has.x > 0.5f) {
        /* This face touches an edge the ink runs from: measure exactly, per pixel. */
        float3 A = attr_face_a.xyz;
        float3 B = attr_face_b.xyz;
        float2 ab = (B - A).xy;
        float t = saturate(dot((R - A).xy, ab) / max(dot(ab, ab), 1e-12f));
        float3 C = A + (B - A) * t;
        float2 tang = (dot(ab, ab) > 1e-12f) ? normalize(ab) : surf_tang;
        leak = enso_ink_drip(
            C, C.z - R.z, R, tang, freq, sv, drip_variation, rain, rain_length, smudge, m_per_px, width);
      }
      else {
        /* Dense meshes: the edge above, carried by the points. */
        float3 A = attr_anchor.xyz;
        float3 off = R - A;
        float h = length(off.xy);
        float spread = max(width, 0.002f) * 3.0f + rain_length * 0.1f;
        leak = enso_ink_drip(
            A, attr_drop.x, R, surf_tang, freq, sv, drip_variation, rain, rain_length, smudge, m_per_px, width);
        leak *= 1.0f - smoothstep(spread * 0.5f, spread, h);
      }
    }
  }
  mask = saturate(max(line, leak));
}
