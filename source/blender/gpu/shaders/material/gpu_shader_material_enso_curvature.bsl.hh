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

#include "gpu_shader_material_enso_npr.bsl.hh"
#include "gpu_shader_material_interface.bsl.hh"

/* Since 1.1 this node shares the curvature of Ensō Screen Curvature: edges and cavities come
 * from the normals around the pixel, so no modifier or extra mesh data is needed. The old mesh
 * attributes are still accepted so existing materials keep working. */
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
  float c;
  float touch;
  enso_curvature_core(kg, sd, radius, 0.1f, 0.6f, c, touch);
  /* Contrast 1 shows a right-angle edge at full strength. */
  float gain = max(contrast, 0.0f) * 4.0f;
  convex = enso_curvature_shape(c, 0.04f, gain);
  contact_out = saturate(touch * max(contrast, 0.0f));
  concave = max(enso_curvature_shape(-c, 0.04f, gain), contact_out * saturate(contact));
  curvature = saturate(0.5f + 0.5f * (convex - concave));
}
