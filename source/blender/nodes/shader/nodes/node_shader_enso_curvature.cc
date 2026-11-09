/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō Curvature: convex and concave masks from the surface curvature (normal change per meter).
 * Measured on the surface, so it stays on the object when the camera moves.
 */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_enso_curvature_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Radius"_ustr)
      .default_value(0.05f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_DISTANCE)
      .description("Width of the edge and cavity masks on the object, in meters");
  b.add_input<decl::Float>("Contrast"_ustr)
      .default_value(2.0f)
      .min(0.0f)
      .max(20.0f)
      .description("Strength of the masks: higher values also catch gentle bends");
  b.add_input<decl::Float>("Contact"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "How much the crease where this object touches or cuts into another one is added to "
          "Concave, like the two were one welded shape");
  b.add_output<decl::Float>("Convex"_ustr).description("Outer edges and ridges");
  b.add_output<decl::Float>("Concave"_ustr).description("Cavities and creases");
  b.add_output<decl::Float>("Curvature"_ustr)
      .description("Signed curvature: 0.5 is flat, above is convex, below is concave");
  b.add_output<decl::Float>("Contact"_ustr)
      .description("Where this object touches or cuts into another object");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  /* Mesh bend from the "Ensō Edge Data" modifier (added automatically). */
  GPUNodeLink *curv = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_curv");
  GPUNodeLink *data = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_data");
  return GPU_stack_link(mat,
                        node,
                        "node_enso_curvature",
                        in,
                        out,
                        curv,
                        data,
                        GPU_kernel_globals(),
                        GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_curvature_cc

void register_node_type_sh_enso_curvature()
{
  namespace file_ns = nodes::node_shader_enso_curvature_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoCurvature"_ustr);
  ntype.ui_name = "Ensō Curvature";
  ntype.ui_description = "Convex and concave masks from the surface curvature (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

}  // namespace blender
