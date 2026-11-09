/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō Brush Normals: painterly normals made of layered brush strokes, scattered on the object.
 */

#include "node_shader_util.hh"

#include "DNA_image_types.h"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"

namespace blender {

namespace nodes::node_shader_enso_brush_normals_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Scale"_ustr)
      .default_value(6.0f)
      .min(0.0f)
      .max(1000.0f)
      .description("Strokes per meter");
  b.add_input<decl::Float>("Length"_ustr)
      .default_value(1.1f)
      .min(0.05f)
      .max(5.0f)
      .description("Stroke length, relative to the stroke spacing");
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.35f)
      .min(0.02f)
      .max(2.0f)
      .description("Stroke width, relative to the stroke spacing");
  b.add_input<decl::Float>("Strength"_ustr)
      .default_value(0.6f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Bristles"_ustr)
      .default_value(4.0f)
      .min(0.0f)
      .max(32.0f)
      .description("Number of bristle ridges across a stroke");
  b.add_input<decl::Float>("Randomness"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the strokes turn away from the flow direction");
  b.add_input<decl::Vector>("Flow"_ustr)
      .default_value({0.0f, 0.0f, 1.0f})
      .description("Direction the strokes follow (world space)");
  b.add_input<decl::Float>("Seed"_ustr).default_value(0.0f);
  b.add_input<decl::Float>("Atlas Columns"_ustr)
      .default_value(2.0f)
      .min(1.0f)
      .max(64.0f)
      .description("Brushes across the brush atlas image");
  b.add_input<decl::Float>("Atlas Rows"_ustr)
      .default_value(8.0f)
      .min(1.0f)
      .max(64.0f)
      .description("Brushes down the brush atlas image");
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_output<decl::Vector>("Normal"_ustr);
  b.add_output<decl::Float>("Height"_ustr).description("Paint thickness");
  b.add_output<decl::Float>("Stroke"_ustr).description("Random value per stroke, for color variation");
}

static void node_buts(ui::Layout &layout, bContext *C, PointerRNA *ptr)
{
  /* Brush atlas: a grid of white-on-black brush strokes, each drawn left to right. Without an
   * image the node paints built in bristle strokes. */
  template_id(&layout, C, ptr, "image", "IMAGE_OT_new", "IMAGE_OT_open", nullptr);
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[10].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[10].link);
  }
  Image *ima = reinterpret_cast<Image *>(node->id);
  if (ima == nullptr) {
    return GPU_stack_link(
        mat, node, "node_enso_brush_normals", in, out, GPU_kernel_globals(), GPU_shading_data());
  }
  GPUSamplerState sampler = GPUSamplerState::default_sampler();
  sampler.filtering = GPU_SAMPLER_FILTERING_LINEAR | GPU_SAMPLER_FILTERING_MIPMAP;
  sampler.extend_x = GPU_SAMPLER_EXTEND_MODE_EXTEND;
  sampler.extend_yz = GPU_SAMPLER_EXTEND_MODE_EXTEND;
  return GPU_stack_link(mat,
                        node,
                        "node_enso_brush_normals_image",
                        in,
                        out,
                        GPU_kernel_globals(),
                        GPU_shading_data(),
                        GPU_image(mat, ima, nullptr, sampler));
}

}  // namespace nodes::node_shader_enso_brush_normals_cc

void register_node_type_sh_enso_brush_normals()
{
  namespace file_ns = nodes::node_shader_enso_brush_normals_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoBrushNormals"_ustr);
  ntype.ui_name = "Ensō Brush Normals";
  ntype.ui_description = "Painterly normals made of layered brush strokes (EEVEE)";
  ntype.nclass = NODE_CLASS_OP_VECTOR;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.draw_buttons = file_ns::node_buts;
  ntype.default_width = 160.0f;
  bke::node_register_type(ntype);
}

}  // namespace blender
