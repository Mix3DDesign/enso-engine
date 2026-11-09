/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō Curvature node: convex edges and concave cavities of the surface, plus the crease where
 * two objects meet (Contact).
 *
 * Two parts: the bend of the mesh itself (from the "Ensō Edge Data" modifier, added automatically:
 * smooth, stable from every view, follows the object), plus sharp corners found in the EEVEE
 * pre-pass within the radius, so flat shaded low-poly corners get a crisp edge too.
 *
 * The node looks at the surface around the shaded point within a radius in meters (taken from the
 * EEVEE pre-pass, so hard edges of low-poly and flat shaded meshes are found too) and measures
 * how the normal turns towards or away from the neighbouring points. The radius is on the object,
 * so the edges keep their width when the camera moves. When the pre-pass is not available the
 * node falls back to the normal change across the pixel. */

#pragma once

#include "gpu_shader_material_interface.bsl.hh"

/* Pixel offset that moves along the surface by a world offset, from the screen derivatives of the
 * position (so it uses the same pixel layout as the pre-pass, on every GPU backend). */
float2 enso_curv_pixel_offset(float3 dPdx, float3 dPdy, float3 offset)
{
  float a = dot(dPdx, dPdx);
  float b = dot(dPdx, dPdy);
  float c = dot(dPdy, dPdy);
  float det = a * c - b * b;
  if (abs(det) < 1e-20f) {
    return float2(0.0f);
  }
  float u = dot(dPdx, offset);
  float v = dot(dPdy, offset);
  return float2(c * u - b * v, a * v - b * u) / det;
}

[[node]]
void node_enso_curvature(float radius,
                         float contrast,
                         float contact,
                         float4 attr_curv,
                         float4 attr_data,
                         [[resource_table]] KernelGlobals &kg,
                         const ShadingData &sd,
                         float &convex,
                         float &concave,
                         float &curvature,
                         float &contact_out)
{
  convex = 0.0f;
  concave = 0.0f;
  curvature = 0.5f;
  contact_out = 0.0f;
  float r = max(radius, 1e-5f);
  float gain = max(contrast, 0.0f);
  float3 P = sd.P;
  /* Bend of the mesh (1 / meters) times the radius: 1 where the surface turns a lot within it. */
  float k_mesh = (attr_data.x > 0.5f) ? attr_curv.x * r : 0.0f;
  float e_convex = 0.0f;
  float e_concave = 0.0f;
  float e_contact = 0.0f;

  float2 size;
  enso_prepass_size(kg, size);
  bool v0;
  float3 N0;
  float d0;
  uint id0;
  enso_prepass_fetch(kg, sd.frag_co.xy, v0, N0, d0, id0);
#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  const ViewMatrices view = kg.view_matrices_get(sd);
  float zv = abs((view.viewmat * float4(P, 1.0f)).z);
  if (v0 && d0 < 1e9f && size.x > 1.0f && zv > 1e-6f) {
    /* The pre-pass depth unit, so every depth test below is in meters. */
    float unit = d0 / zv;
    float3 dPdx = gpu_dfdx(P);
    float3 dPdy = gpu_dfdy(P);
    float3 Ng = normalize(float3(sd.Ng));
    float3 T = normalize(abs(Ng.z) < 0.9f ? cross(Ng, float3(0.0f, 0.0f, 1.0f)) :
                                            cross(Ng, float3(1.0f, 0.0f, 0.0f)));
    float3 B = cross(Ng, T);
    float2 p0 = sd.frag_co.xy;
    bool persp = view.winmat[3][3] == 0.0f;
    float3 V = persp ? normalize(view.viewinv[3].xyz - P) : normalize(view.viewinv[2].xyz);
    /* How the view depth changes per pixel on this face: predicts where the face would be at a
     * neighbouring pixel (in 1 / depth, which is exactly linear across the screen for a plane),
     * and how much a single pixel step can move it (the tolerance at grazing angles). */
    float3 vz = float3(view.viewmat[0][2], view.viewmat[1][2], view.viewmat[2][2]);
    float2 dz_px = float2(abs(dot(vz, dPdx)), abs(dot(vz, dPdy)));
    float2 dinv = -float2(dot(vz, dPdx), dot(vz, dPdy)) * (persp ? -1.0f : 0.0f) / (zv * zv);
    float px_slack = (dz_px.x + dz_px.y) * 1.5f;
    /* Faces seen almost side-on are the outline of a round shape, not an outer corner. */
    float facing = smoothstep(0.15f, 0.35f, abs(dot(N0, V)));
    float jit;
    enso_pixel_jitter(kg, p0, jit);
    const int dirs = 8;
    const int steps = 3;
    for (int i = 0; i < dirs; i++) {
      float a = (float(i) + jit) * (6.2831853f / float(dirs));
      float3 dir = T * cos(a) + B * sin(a);
      /* March out from the point: the first edge met along this direction is the closest one,
       * and its distance sets the strength, so every edge gets a band of the same width. */
      for (int s = 0; s < steps; s++) {
        float t = (float(s) + fract(jit * 5.17f + float(i) * 0.37f)) / float(steps);
        float rr = r * max(t, 0.08f);
        float3 off = dir * rr;
        float2 dq = enso_curv_pixel_offset(dPdx, dPdy, off);
        float lq = length(dq);
        if (lq < 1.0f) {
          dq = (lq > 1e-4f) ? dq / lq : float2(1.0f, 0.0f);
        }
        else if (lq > 128.0f) {
          dq *= 128.0f / lq;
        }
        float2 q = p0 + dq;
        if (any(lessThan(q, float2(0.0f))) || any(greaterThanEqual(q, size))) {
          break;
        }
        bool vq;
        float3 Nq;
        float dqv;
        uint idq;
        enso_prepass_fetch(kg, q, vq, Nq, dqv, idq);
        /* Expected depth at that pixel if the face carried on flat. */
        float z_exp;
        if (persp) {
          float inv = 1.0f / zv + dot(dinv, dq);
          z_exp = (inv > 1e-6f) ? 1.0f / inv : 1e10f;
        }
        else {
          z_exp = zv + dot(-float2(dot(vz, dPdx), dot(vz, dPdy)), dq);
        }
        float dz = (dqv < 1e9f) ? dqv / unit - z_exp : 1e10f;
        float tol = rr * 0.5f + r * 0.15f + px_slack;
        float w = 1.0f - rr / r * 0.85f;
        if (!vq || dqv > 1e9f) {
          /* Empty background there: the surface turns away out of sight. */
          e_convex = max(e_convex, w * facing);
          break;
        }
        if (idq != id0) {
          if (abs(dz) < tol + rr) {
            /* Another object right at the surface: the two meet here (union crease). */
            e_contact = max(e_contact, w);
            break;
          }
          if (dz > 0.0f) {
            /* Something far behind: the outline of this object. */
            e_convex = max(e_convex, w * facing);
            break;
          }
          /* Another object in front covers the sample: its own edge, not ours. */
          break;
        }
        float cosn = dot(Nq, N0);
        if (cosn < 0.87f) {
          float bend = dot(Nq - N0, dir);
          float hard = saturate((0.87f - cosn) * 4.0f);
          if (bend > 0.0f) {
            e_convex = max(e_convex, w * hard);
          }
          else {
            e_concave = max(e_concave, w * hard);
          }
          break;
        }
        if (dz > tol + rr * 1.5f) {
          /* Same object, but a step away from the camera: an outer corner hidden behind. */
          e_convex = max(e_convex, w * facing);
          break;
        }
        if (dz < -(tol + rr * 1.5f)) {
          /* Same object, coming towards the camera: a fold of the surface over this point. */
          e_concave = max(e_concave, w);
          break;
        }
      }
    }
  }
#endif
  float k_conv = max(k_mesh, 0.0f);
  float k_conc = max(-k_mesh, 0.0f);
  float c_conv = saturate(max(e_convex, k_conv) * gain);
  float c_conc = saturate(max(e_concave, k_conc) * gain);
  contact_out = saturate(e_contact * gain);
  convex = c_conv;
  concave = saturate(max(c_conc, contact_out * saturate(contact)));
  curvature = saturate(0.5f + 0.5f * (c_conv - concave));
}
