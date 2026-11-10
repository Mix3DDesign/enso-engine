/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō NPR nodes on the EEVEE pre-pass: Screen Rim, Line Detect and Hatching.
 */

#include "GPU_material.hh"

#include "node_shader_enso_pass.hh"
#include "node_shader_util.hh"

namespace blender {

/* -------------------------------------------------------------------- Ensō Screen Rim */
namespace nodes::node_shader_enso_screen_rim_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.03f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_DISTANCE)
      .description("How far in from the edge the rim reaches, in meters on the object");
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.15f)
      .min(0.0f)
      .max(100.0f)
      .subtype(PROP_DISTANCE)
      .description("How far behind the surface the background must be for a rim, in meters");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Softness of the rim edge");
  b.add_input<decl::Vector>("Direction"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .description(
          "Side the rim appears on, in world space (for example a light's direction). Zero puts "
          "it all around the silhouette");
  b.add_output<decl::Float>("Rim"_ustr).description("1 on the rim, 0 elsewhere");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_screen_rim", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Screen Rim", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_screen_rim_cc

void register_node_type_sh_enso_screen_rim()
{
  namespace file_ns = nodes::node_shader_enso_screen_rim_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoScreenRim"_ustr);
  ntype.ui_name = "Ensō Screen Rim";
  ntype.ui_description = "Anime rim light from the depth behind the silhouette (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Line Detect */
namespace nodes::node_shader_enso_line_detect_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.015f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_DISTANCE)
      .description("Line width in meters on the object, so lines keep their size on it");
  b.add_input<decl::Float>("Crease Angle"_ustr)
      .default_value(0.7f)
      .min(0.0f)
      .max(3.14159f)
      .subtype(PROP_ANGLE)
      .description("Folds sharper than this get a crease line");
  b.add_input<decl::Float>("Depth Threshold"_ustr)
      .default_value(0.1f)
      .min(0.0f)
      .max(100.0f)
      .subtype(PROP_DISTANCE)
      .description("Depth step that makes a silhouette, in meters");
  b.add_output<decl::Float>("Line"_ustr).description("All lines together");
  b.add_output<decl::Float>("Silhouette"_ustr).description("Outer outline against what is behind");
  b.add_output<decl::Float>("Crease"_ustr).description("Folds and sharp edges on the surface");
  b.add_output<decl::Float>("Object Border"_ustr)
      .description("Where this object meets any other object on screen");
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
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_line_detect", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Line", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_line_detect_cc

void register_node_type_sh_enso_line_detect()
{
  namespace file_ns = nodes::node_shader_enso_line_detect_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoLineDetect"_ustr);
  ntype.ui_name = "Ensō Line Detect";
  ntype.ui_description = "Silhouette, crease, object border and contact lines (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Hatching */
namespace nodes::node_shader_enso_hatching_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Shade"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Lightness to draw: 1 is paper white, 0 is full ink (plug lighting in here)");
  b.add_input<decl::Float>("Scale"_ustr)
      .default_value(15.0f)
      .min(0.01f)
      .max(10000.0f)
      .description("Pattern cells per meter on the object");
  b.add_input<decl::Float>("Angle"_ustr)
      .default_value(0.785398f)
      .subtype(PROP_ANGLE)
      .description("Rotation of the pattern");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 gives crisp printed edges, higher softens them");
  b.add_output<decl::Float>("Dots"_ustr).description("Halftone dots, 1 is ink");
  b.add_output<decl::Float>("Lines"_ustr).description("Parallel hatching lines, 1 is ink");
  b.add_output<decl::Float>("Crosshatch"_ustr)
      .description("Hatching that crosses in the darker tones, 1 is ink");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  return GPU_stack_link(
      mat, node, "node_enso_hatching", in, out, GPU_kernel_globals(), GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_hatching_cc

void register_node_type_sh_enso_hatching()
{
  namespace file_ns = nodes::node_shader_enso_hatching_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoHatching"_ustr);
  ntype.ui_name = "Ensō Hatching";
  ntype.ui_description = "Manga halftone dots, hatching lines and crosshatch from a shade value";
  ntype.nclass = NODE_CLASS_TEXTURE;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Screen Curvature */
namespace nodes::node_shader_enso_screen_curvature_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Radius"_ustr)
      .default_value(0.02f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_DISTANCE)
      .description("Size of the edges and cavities in meters on the object");
  b.add_input<decl::Float>("Strength"_ustr)
      .default_value(4.0f)
      .min(0.0f)
      .max(50.0f);
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.06f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "Bends weaker than this are ignored. Raise it to hide the facets of a low-poly or "
          "flat shaded mesh and keep only the real edges");
  b.add_input<decl::Float>("Depth Threshold"_ustr)
      .default_value(0.1f)
      .min(0.0f)
      .max(100.0f)
      .subtype(PROP_DISTANCE)
      .description("Bigger depth steps than this are treated as another surface and ignored");
  b.add_input<decl::Float>("Blur"_ustr)
      .default_value(0.6f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Smooths the result. It removes the dots and lines that the faces of the mesh "
                   "leave when the radius is close to their size");
  b.add_output<decl::Float>("Convex"_ustr)
      .description("Outer edges and corners: wear, highlights");
  b.add_output<decl::Float>("Concave"_ustr)
      .description("Creases and cavities: dirt, occlusion, ink");
  b.add_output<decl::Float>("Signed"_ustr)
      .description("Positive on edges, negative in cavities");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_screen_curvature", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Convex", out[0].link);
    enso_pass_write(mat, "Enso Concave", out[1].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_screen_curvature_cc

void register_node_type_sh_enso_screen_curvature()
{
  namespace file_ns = nodes::node_shader_enso_screen_curvature_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoScreenCurvature"_ustr);
  ntype.ui_name = "Ensō Screen Curvature";
  ntype.ui_description = "Edges and cavities found on screen from the scene normals: no setup, works on any mesh and follows deformation (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Reflection */
namespace nodes::node_shader_enso_reflection_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_input<decl::Float>("Distance"_ustr)
      .default_value(5.0f)
      .min(0.0f)
      .max(1000.0f)
      .subtype(PROP_DISTANCE)
      .description("How far away something can be and still be reflected");
  b.add_input<decl::Float>("Thickness"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(100.0f)
      .subtype(PROP_DISTANCE)
      .description("How thick objects are taken to be. Raise it when reflections have holes");
  b.add_input<decl::Float>("Fade"_ustr)
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Fades the reflection toward the far end of Distance");
  b.add_input<decl::Float>("Reflect Self"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .description("1 lets the object reflect itself too");
  b.add_input<decl::Float>("Stretch"_ustr)
      .default_value(0.6f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 is a true mirror, which hides under the object when seen from above. "
                   "Higher keeps the reflection long and visible from any camera angle");
  b.add_output<decl::Float>("Mask"_ustr)
      .description("1 where something is reflected: the shape of the reflection");
  b.add_output<decl::Color>("Color"_ustr)
      .description("The Object Color of what is reflected (Object properties > Viewport "
                   "Display > Color): give each object the colour its reflection should have");
  b.add_output<decl::Float>("Distance"_ustr)
      .description("How far the reflected thing is, 0 near to 1 at the end of Distance");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[0].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[0].link);
  }
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_reflection", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Reflection", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_reflection_cc

void register_node_type_sh_enso_reflection()
{
  namespace file_ns = nodes::node_shader_enso_reflection_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoReflection"_ustr);
  ntype.ui_name = "Ensō Reflection";
  ntype.ui_description =
      "What the surface would reflect, as a mask and a flat colour per object: paint fake "
      "reflections on floors, water and glass. Only things inside the camera view can be "
      "reflected (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Blur */
namespace nodes::node_shader_enso_blur_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Value"_ustr)
      .default_value({0.0f, 0.0f, 0.0f, 1.0f})
      .hide_value()
      .description("Anything to blur: a colour, a value or a vector from any textures and nodes");
  b.add_input<decl::Float>("Radius"_ustr)
      .default_value(8.0f)
      .min(0.0f)
      .max(64.0f)
      .description("Blur size in screen pixels");
  b.add_output<decl::Color>("Value"_ustr)
      .description("The blurred result. Plug it into a colour, value or vector input");
}

static int node_gpu(GPUMaterial *mat,
                    bNode * /*node*/,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[0].link) {
    static float nothing[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    return GPU_link(mat, "set_rgba", GPU_constant(nothing), &out[0].link);
  }
  /* Everything plugged into Value becomes a function, so it can be evaluated again around the
   * pixel. Its textures read their coordinates moved by a few pixels each time. */
  const char *value_function = GPU_material_split_sub_function(mat, GPU_VEC4, &in[0].link);
  GPUNodeLink *ready = nullptr;
  GPU_link(mat,
           "node_enso_blur_setup",
           in[1].link ? in[1].link : GPU_uniform(in[1]),
           &ready);
  return GPU_link(
      mat, "node_enso_blur", ready, GPU_enso_blur_function(value_function), &out[0].link);
}

}  // namespace nodes::node_shader_enso_blur_cc

void register_node_type_sh_enso_blur()
{
  namespace file_ns = nodes::node_shader_enso_blur_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoBlur"_ustr);
  ntype.ui_name = "Ensō Blur";
  ntype.ui_description =
      "Blurs whatever is plugged into it, like a blur filter: textures, colours, values and "
      "vectors. The size is in screen pixels (EEVEE)";
  ntype.nclass = NODE_CLASS_OP_COLOR;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 150.0f;
  bke::node_register_type(ntype);
}

}  // namespace blender
