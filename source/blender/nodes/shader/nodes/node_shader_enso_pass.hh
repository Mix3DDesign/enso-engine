/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup shdnodes
 *
 * Ensō passes: the Ensō shading nodes write what they compute into render passes of the same
 * name, so the compositor gets the shade, the shadows, the highlight or the lines of a frame
 * as separate layers without any extra nodes in the material.
 *
 * A pass is an ordinary AOV: it only costs something once a pass with that name is turned on
 * in the view layer.
 */

#include <cstring>
#include <iterator>

#include "BLI_hash_c.hh"

#include "GPU_material.hh"

namespace blender::nodes {

/** Pass names in the order of their slot in the shader's running values. */
inline const char *const enso_pass_names[] = {
    "Enso Shade",
    "Enso Light",
    "Enso Cast Shadow",
    "Enso Form Shadow",
    "Enso Cel",
    "Enso Face Shade",
    "Enso Hair Ring",
    "Enso Highlight",
    "Enso Rim",
    "Enso Screen Rim",
    "Enso Line",
    "Enso Convex",
    "Enso Concave",
    "Enso Mask",
    "Enso Face",
    "Enso Hair",
    "Enso Eye",
    "Enso Skin",
    "Enso Cloth",
    "Enso Reflection",
};

/**
 * Write a value into the pass called \a name.
 *
 * The pass keeps the highest value written for the pixel. Inside the light loop that makes a
 * node add up over all lights (lit by any light), and it lets several nodes of one kind share
 * a pass.
 */
inline void enso_pass_write(GPUMaterial *mat, const char *name, GPUNodeLink *value)
{
  if (value == nullptr) {
    return;
  }
  float slot = -1.0f;
  for (int i = 0; i < int(std::size(enso_pass_names)); i++) {
    if (std::strcmp(enso_pass_names[i], name) == 0) {
      slot = float(i);
      break;
    }
  }
  if (slot < 0.0f) {
    return;
  }
  uint hash = BLI_hash_string(name);
  /* Same aliasing as the AOV Output node: the hash travels as a float and is cast back. */
  GPUNodeLink *hash_link = GPU_constant(reinterpret_cast<float *>(&hash));
  GPUNodeLink *outlink = nullptr;
  GPU_material_flag_set(mat, GPU_MATFLAG_AOV | GPU_MATFLAG_OBJECT_INFO);
  GPU_link(mat,
           "node_enso_pass",
           value,
           GPU_constant(&slot),
           hash_link,
           GPU_kernel_globals(),
           GPU_shading_data(),
           &outlink);
  if (outlink) {
    GPU_material_add_output_link_aov(mat, outlink, hash);
  }
}

/** Write a colour into the colour pass called \a name (an ordinary colour AOV). */
inline void enso_pass_write_color(GPUMaterial *mat, const char *name, GPUNodeLink *color)
{
  if (color == nullptr) {
    return;
  }
  uint hash = BLI_hash_string(name);
  GPUNodeLink *hash_link = GPU_constant(reinterpret_cast<float *>(&hash));
  GPUNodeLink *outlink = nullptr;
  float zero = 0.0f;
  GPU_material_flag_set(mat, GPU_MATFLAG_AOV | GPU_MATFLAG_OBJECT_INFO);
  GPU_link(mat,
           "node_output_aov",
           color,
           GPU_constant(&zero),
           hash_link,
           GPU_kernel_globals(),
           GPU_shading_data(),
           &outlink);
  if (outlink) {
    GPU_material_add_output_link_aov(mat, outlink, hash);
  }
}

}  // namespace blender::nodes
