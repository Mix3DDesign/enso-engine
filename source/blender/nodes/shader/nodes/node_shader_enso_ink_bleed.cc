/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō Ink Bleed: ink that leaks down from the sharp upper edges of an object in soft drips,
 * measured on the object (from the "Ensō Edge Data" modifier), plus optional edge ink and outline.
 */

#include "GPU_material.hh"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_enso_ink_bleed_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.02f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_DISTANCE)
      .description("Width of the ink along the edges and of the outline, in meters");
  b.add_input<decl::Float>("Crease"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Ink lines on folds and corners");
  b.add_input<decl::Float>("Outline"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Ink lines on the outer edges of the object");
  b.add_input<decl::Float>("Detail"_ustr)
      .default_value(8.0f)
      .min(0.0f)
      .max(1000.0f)
      .description("How many drips per meter along the edge");
  b.add_input<decl::Float>("Variation"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 gives even lines; higher makes the line width uneven, like hand-drawn ink");
  b.add_input<decl::Float>("Rain"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much ink bleeds down from the lines");
  b.add_input<decl::Float>("Rain Length"_ustr)
      .default_value(0.25f)
      .min(0.0f)
      .max(100.0f)
      .subtype(PROP_DISTANCE)
      .description("Longest drip, in meters");
  b.add_input<decl::Float>("Drip Variation"_ustr)
      .default_value(0.7f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 gives an even band of ink; higher makes some drips run much further");
  b.add_input<decl::Float>("Smudge"_ustr)
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Softer, slightly wobbly drip edges");
  b.add_input<decl::Float>("Seed"_ustr).default_value(0.0f);
  b.add_output<decl::Float>("Mask"_ustr).description("Edge ink and drips together");
  b.add_output<decl::Float>("Line"_ustr).description("Edge ink and outline only");
  b.add_output<decl::Float>("Leak"_ustr).description("Drips only");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  /* Point attributes written by the "Ensō Edge Data" modifier (added automatically). */
  GPUNodeLink *anchor = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_leak_anchor");
  GPUNodeLink *drop = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_leak_drop");
  GPUNodeLink *rest = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_leak_rest");
  GPUNodeLink *valid = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_leak_valid");
  GPUNodeLink *face_a = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_face_a");
  GPUNodeLink *face_b = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_face_b");
  GPUNodeLink *face_has = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_face_has");
  return GPU_stack_link(mat,
                        node,
                        "node_enso_ink_bleed",
                        in,
                        out,
                        anchor,
                        drop,
                        rest,
                        valid,
                        face_a,
                        face_b,
                        face_has,
                        GPU_kernel_globals(),
                        GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_ink_bleed_cc

void register_node_type_sh_enso_ink_bleed()
{
  namespace file_ns = nodes::node_shader_enso_ink_bleed_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeEnsoInkBleed"_ustr);
  ntype.ui_name = "Ensō Ink Bleed";
  ntype.ui_description =
      "Ink lines on creases and outlines, with ink that bleeds down from them in soft drips "
      "that stick to the object";
  ntype.nclass = NODE_CLASS_TEXTURE;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

}  // namespace blender
