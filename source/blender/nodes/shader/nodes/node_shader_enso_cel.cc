/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō clean cel shading nodes: Cel Bands, Smooth Normals and Face Shadow.
 */

#include "node_shader_enso_pass.hh"
#include "node_shader_util.hh"

#include "BLI_listbase_iterator.hh"

#include "BKE_global.hh"

#include "DNA_layer_types.h"
#include "DNA_light_types.h"
#include "DNA_object_types.h"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {

/* -------------------------------------------------------------------- Ensō Cel Bands */
namespace nodes::node_shader_enso_cel_bands_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Value"_ustr)
      .default_value(0.5f)
      .hide_value()
      .description("Lighting to band, for example the Factor of a Light Evaluation node. Left "
                   "unconnected, the node lights the surface itself from Light Direction");
  b.add_input<decl::Float>("Bands"_ustr)
      .default_value(1.0f)
      .min(1.0f)
      .max(8.0f)
      .description("Number of shadow steps: 1 is light and shadow, 2 adds a half tone");
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.15f)
      .min(-1.0f)
      .max(2.0f)
      .description("Where the shadow starts. Higher makes the shadow bigger");
  b.add_input<decl::Float>("Spread"_ustr)
      .default_value(0.25f)
      .min(0.0f)
      .max(2.0f)
      .description("Distance between the steps when there is more than one band");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Painted softness of each step. 0 is a hard anime edge");
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description(
          "Width of the smoothed edge in screen pixels. The edge stays this crisp at any zoom, "
          "with no stair steps");
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({-0.4f, -0.5f, 0.75f})
      .description("Used when Value is not connected: the direction toward the light, for "
                   "example the Direction of an Ensō Light Select node. This shading has no "
                   "cast shadows, so it never flickers");
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_output<decl::Float>("Shade"_ustr).description("0 in full shadow, 1 in full light");
  b.add_output<decl::Float>("Edge"_ustr)
      .description("1 on the thin line where light meets shadow, for bleed and edge tint");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[7].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[7].link);
  }
  if (!in[0].link) {
    /* Nothing to band: light the surface from Light Direction. */
    GPU_link(mat,
             "node_enso_cel_own_light",
             in[7].link,
             in[6].link ? in[6].link : GPU_uniform(in[6]),
             &in[0].link);
  }
  const bool ok = GPU_stack_link(mat, node, "node_enso_cel_bands", in, out);
  if (ok) {
    enso_pass_write(mat, "Enso Cel", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_cel_bands_cc

void register_node_type_sh_enso_cel_bands()
{
  namespace file_ns = nodes::node_shader_enso_cel_bands_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoCelBands"_ustr);
  ntype.ui_name = "Ensō Cel Bands";
  ntype.ui_description =
      "Cel shading steps with pixel-smooth edges that never stair-step (EEVEE)";
  ntype.nclass = NODE_CLASS_CONVERTER;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Smooth Normals */
namespace nodes::node_shader_enso_smooth_normals_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Roundness"_ustr)
      .default_value(0.8f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 keeps the mesh normals, 1 shades the object like a smooth ball");
  b.add_input<decl::Vector>("Center"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Middle of the round shape, in the object's own space");
  b.add_input<decl::Vector>("Stretch"_ustr)
      .default_value({1.0f, 1.0f, 1.0f})
      .subtype(PROP_XYZ)
      .description("Shape of the ball along the object's X, Y and Z, for long heads and bodies");
  b.add_input<decl::Float>("Tube"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "0 is a ball, 1 is a tube around the object's Z axis. Use a tube for hair so the "
          "light makes clean rings");
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_output<decl::Vector>("Normal"_ustr);
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[4].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[4].link);
  }
  return GPU_stack_link(
      mat, node, "node_enso_smooth_normals", in, out, GPU_kernel_globals(), GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_smooth_normals_cc

void register_node_type_sh_enso_smooth_normals()
{
  namespace file_ns = nodes::node_shader_enso_smooth_normals_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoSmoothNormals"_ustr);
  ntype.ui_name = "Ensō Smooth Normals";
  ntype.ui_description =
      "Clean rounded normals for anime faces, hair and clothes, with no baking (EEVEE)";
  ntype.nclass = NODE_CLASS_OP_VECTOR;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 160.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Face Shadow */
namespace nodes::node_shader_enso_face_shadow_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -1.0f, 1.0f})
      .description("Direction toward the light: the Direction of a Light Evaluation node");
  b.add_input<decl::Vector>("Forward"_ustr)
      .default_value({0.0f, -1.0f, 0.0f})
      .description("Where the face looks, in the object's own space");
  b.add_input<decl::Vector>("Up"_ustr)
      .default_value({0.0f, 0.0f, 1.0f})
      .description("Top of the head, in the object's own space");
  b.add_input<decl::Float>("Roundness"_ustr)
      .default_value(0.6f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Higher gives a simpler, cleaner shadow. Lower keeps the nose and brow");
  b.add_input<decl::Vector>("Center"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Middle of the head, in the object's own space");
  b.add_input<decl::Float>("Level Light"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "1 ignores how high the light is, so the shadow only sweeps left and right like "
          "drawn anime faces. 0 uses the real light direction");
  b.add_input<decl::Float>("Offset"_ustr)
      .default_value(0.0f)
      .min(-1.0f)
      .max(1.0f)
      .description("Moves the shadow edge. Higher makes the shadow bigger");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_output<decl::Float>("Shade"_ustr).description("0 in shadow, 1 in light");
  b.add_output<decl::Float>("Map Threshold"_ustr)
      .description(
          "For a drawn face shadow map (SDF): the face is lit where the map is greater than "
          "this value");
  b.add_output<decl::Float>("Flip"_ustr)
      .description(
          "For a drawn face shadow map: 1 when the light is on the other side and the map "
          "should be mirrored left to right");
  b.add_output<decl::Vector>("Normal"_ustr).description("The rounded face normal");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[9].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[9].link);
  }
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_face_shadow", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Face Shade", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_face_shadow_cc

void register_node_type_sh_enso_face_shadow()
{
  namespace file_ns = nodes::node_shader_enso_face_shadow_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoFaceShadow"_ustr);
  ntype.ui_name = "Ensō Face Shadow";
  ntype.ui_description =
      "Anime face shadow that sweeps cleanly as the light turns, with no painting or baking "
      "(EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Hair Ring */
namespace nodes::node_shader_enso_hair_ring_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -1.0f, 1.0f})
      .description("Direction toward the light: the Direction of a Light Evaluation node");
  b.add_input<decl::Vector>("Up"_ustr)
      .default_value({0.0f, 0.0f, 1.0f})
      .description("Top of the head, in the object's own space. The hair flows away from it");
  b.add_input<decl::Vector>("Center"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Middle of the head, in the object's own space");
  b.add_input<decl::Float>("Position"_ustr)
      .default_value(0.65f)
      .min(-2.0f)
      .max(2.0f)
      .description("Moves the ring up and down the hair");
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.12f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Thickness of the ring");
  b.add_input<decl::Float>("Strands"_ustr)
      .default_value(0.5f)
      .hide_value()
      .description(
          "A texture that is different for each lock of hair (0 to 1). It breaks the ring into "
          "the zigzag drawn in anime");
  b.add_input<decl::Float>("Strand Amount"_ustr)
      .default_value(0.35f)
      .min(0.0f)
      .max(2.0f)
      .description("How far the Strands texture pushes the ring up and down");
  b.add_input<decl::Float>("Locks"_ustr)
      .default_value(28.0f)
      .min(0.0f)
      .max(200.0f)
      .description(
          "Built-in zigzag: number of hair locks around the head. 0 turns it off so only the "
          "Strands texture shapes the ring");
  b.add_input<decl::Float>("Follow Light"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 keeps the ring facing the camera, 1 lets it move fully with the light");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_output<decl::Float>("Ring"_ustr).description("1 on the highlight ring");
  b.add_output<decl::Float>("Offset"_ustr)
      .description(
          "Signed distance from the middle of the ring. Add it to the V of a painted highlight "
          "mask so the drawing slides with the camera and light");
  b.add_output<decl::Vector>("Tangent"_ustr)
      .description("Direction the hair flows over the head, for your own highlights");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[11].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[11].link);
  }
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_hair_ring", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Hair Ring", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_hair_ring_cc

void register_node_type_sh_enso_hair_ring()
{
  namespace file_ns = nodes::node_shader_enso_hair_ring_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoHairRing"_ustr);
  ntype.ui_name = "Ensō Hair Ring";
  ntype.ui_description =
      "The anime hair highlight ring, clean at any zoom, with a zigzag from a strand texture "
      "(EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Face Map */
namespace nodes::node_shader_enso_face_map_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Map"_ustr)
      .default_value({1.0f, 1.0f, 1.0f, 1.0f})
      .hide_value()
      .description(
          "The face shadow map. Red is used when the light is on the character's right, green "
          "when it is on the left. A grey map works for both sides");
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .description("Connect the Map Threshold of Ensō Face Shadow");
  b.add_input<decl::Float>("Flip"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .description("Connect the Flip of Ensō Face Shadow");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_output<decl::Float>("Shade"_ustr).description("0 in shadow, 1 in light");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  const bool ok = GPU_stack_link(mat, node, "node_enso_face_map", in, out);
  if (ok) {
    enso_pass_write(mat, "Enso Face Shade", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_face_map_cc

void register_node_type_sh_enso_face_map()
{
  namespace file_ns = nodes::node_shader_enso_face_map_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoFaceMap"_ustr);
  ntype.ui_name = "Ensō Face Map";
  ntype.ui_description =
      "Reads a drawn or baked face shadow map (SDF) with a clean, pixel-smooth edge (EEVEE)";
  ntype.nclass = NODE_CLASS_CONVERTER;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Mask */
namespace nodes::node_shader_enso_mask_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Center"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Middle of the box or ball");
  b.add_input<decl::Vector>("Size"_ustr)
      .default_value({1.0f, 1.0f, 1.0f})
      .subtype(PROP_XYZ)
      .description("Half size of the box or ball on each axis");
  b.add_input<decl::Float>("Roundness"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 is a box, 1 is a ball");
  b.add_input<decl::Float>("Height Min"_ustr)
      .default_value(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Height where the Height mask starts");
  b.add_input<decl::Float>("Height Max"_ustr)
      .default_value(1.0f)
      .subtype(PROP_DISTANCE)
      .description("Height where the Height mask is full. Same as Min gives a hard cut");
  b.add_input<decl::Vector>("Up"_ustr)
      .default_value({0.0f, 0.0f, 1.0f})
      .description("Direction that counts as up for Height and Slope");
  b.add_input<decl::Float>("Slope Angle"_ustr)
      .default_value(0.7854f)
      .min(0.0f)
      .max(3.14159f)
      .subtype(PROP_ANGLE)
      .description("Surfaces within this angle from Up are selected by Slope");
  b.add_input<decl::Float>("World Space"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 measures on the object so the mask moves with it, 1 measures in the world");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value();
  b.add_output<decl::Float>("Shape"_ustr)
      .description("1 inside the box or ball");
  b.add_output<decl::Float>("Height"_ustr)
      .description("0 below Height Min, 1 above Height Max");
  b.add_output<decl::Float>("Slope"_ustr)
      .description("1 where the surface faces Up");
  b.add_output<decl::Float>("Facing"_ustr)
      .description("1 where the surface faces the camera, 0 at the silhouette");
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
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_mask", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Mask", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_mask_cc

void register_node_type_sh_enso_mask()
{
  namespace file_ns = nodes::node_shader_enso_mask_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoMask"_ustr);
  ntype.ui_name = "Ensō Mask";
  ntype.ui_description = "Select part of a surface inside a material: a box or ball, a height range, a slope or the side facing the camera, all with a clean edge (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Shade Info */
namespace nodes::node_shader_enso_shade_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -1.0f, 1.0f})
      .description("Direction toward the light: the Direction of a Light Evaluation node");
  b.add_input<decl::Float>("Mask"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .hide_value()
      .description("The Mask of a Light Evaluation node: cast shadows and spot cone");
  b.add_input<decl::Float>("Terminator"_ustr)
      .default_value(0.0f)
      .min(-1.0f)
      .max(1.0f)
      .description("Moves the line between light and shadow. Above 0 gives more shadow");
  b.add_input<decl::Float>("Wrap"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Lets the light reach around the form (half Lambert at 1)");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value();
  b.add_input<decl::Float>("Shadow Distance"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1000.0f)
      .subtype(PROP_DISTANCE)
      .description("The node looks this far toward the light for something that blocks it, and "
                   "finds contact shadows by itself. They are found on screen, so they only see "
                   "what is inside the camera view and they change as the view moves. 0 is off");
  b.add_output<decl::Float>("Shade"_ustr)
      .description("Light with all shadows: 1 lit, 0 in any shadow");
  b.add_output<decl::Float>("Light"_ustr)
      .description("Lit side of the form only, ignoring cast shadows");
  b.add_output<decl::Float>("Cast Shadow"_ustr)
      .description("1 where another surface blocks the light on the lit side");
  b.add_output<decl::Float>("Form Shadow"_ustr)
      .description("1 on the side turned away from the light");
  b.add_output<decl::Float>("Facing Light"_ustr)
      .description("Raw value: 1 facing the light, -1 facing away");
  b.add_output<decl::Float>("Self Shadow"_ustr)
      .description("1 where the object shadows itself on the lit side");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[6].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[6].link);
  }
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_shade_info", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Shade", out[0].link);
    enso_pass_write(mat, "Enso Light", out[1].link);
    enso_pass_write(mat, "Enso Cast Shadow", out[2].link);
    enso_pass_write(mat, "Enso Form Shadow", out[3].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_shade_info_cc

void register_node_type_sh_enso_shade_info()
{
  namespace file_ns = nodes::node_shader_enso_shade_info_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoShadeInfo"_ustr);
  ntype.ui_name = "Ensō Shade Info";
  ntype.ui_description = "Splits one light into its parts: the lit side, the form shadow and the cast shadow, each with a clean edge, so every part can get its own colour (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Stylized Highlight */
namespace nodes::node_shader_enso_highlight_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -1.0f, 1.0f})
      .description("Direction toward the light: the Direction of a Light Evaluation node");
  b.add_input<decl::Vector>("Up"_ustr)
      .default_value({0.0f, 0.0f, 1.0f})
      .description("Up of the object, in its own space. Sets which way Stretch and Offset work");
  b.add_input<decl::Float>("Size"_ustr)
      .default_value(0.15f)
      .min(0.0f)
      .max(2.0f);
  b.add_input<decl::Float>("Stretch X"_ustr)
      .default_value(1.0f)
      .min(0.01f)
      .max(20.0f)
      .description("Widens the highlight sideways");
  b.add_input<decl::Float>("Stretch Y"_ustr)
      .default_value(1.0f)
      .min(0.01f)
      .max(20.0f)
      .description("Lengthens the highlight up and down");
  b.add_input<decl::Float>("Offset X"_ustr)
      .default_value(0.0f)
      .min(-2.0f)
      .max(2.0f);
  b.add_input<decl::Float>("Offset Y"_ustr)
      .default_value(0.0f)
      .min(-2.0f)
      .max(2.0f);
  b.add_input<decl::Float>("Split"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .description("Pulls the highlight apart into two");
  b.add_input<decl::Float>("Squareness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 is round, 1 is a soft square");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value();
  b.add_output<decl::Float>("Highlight"_ustr)
      .description("1 inside the highlight");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[11].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[11].link);
  }
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_highlight", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Highlight", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_highlight_cc

void register_node_type_sh_enso_highlight()
{
  namespace file_ns = nodes::node_shader_enso_highlight_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoHighlight"_ustr);
  ntype.ui_name = "Ensō Stylized Highlight";
  ntype.ui_description = "A drawn highlight you can move, stretch, split in two and square off, like the ones painted on anime hair, eyes and metal (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Light Rim */
namespace nodes::node_shader_enso_light_rim_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -1.0f, 1.0f})
      .description("Direction toward the light: the Direction of a Light Evaluation node");
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(0.25f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Light Side"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("1 keeps the rim on the lit side only, 0 draws it all around");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value();
  b.add_output<decl::Float>("Rim"_ustr)
      .description("1 on the rim");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  if (!in[5].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[5].link);
  }
  const bool ok = GPU_stack_link(
      mat, node, "node_enso_light_rim", in, out, GPU_kernel_globals(), GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Rim", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_light_rim_cc

void register_node_type_sh_enso_light_rim()
{
  namespace file_ns = nodes::node_shader_enso_light_rim_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoLightRim"_ustr);
  ntype.ui_name = "Ensō Light Rim";
  ntype.ui_description = "Rim light on the edge of the form, only on the side the light comes from (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō hair strands */

/* Mesh data written by "Make Hair Strand Data": the strand UV map, the random value per strand
 * and the active UV map as the fallback. */
static void enso_hair_strand_links(GPUMaterial *mat,
                                   GPUNodeLink **r_strand_uv,
                                   GPUNodeLink **r_strand_id,
                                   GPUNodeLink **r_uv)
{
  *r_strand_uv = GPU_attribute(mat, CD_AUTO_FROM_NAME, "Enso Hair Strand");
  *r_strand_id = GPU_attribute(mat, CD_AUTO_FROM_NAME, "enso_strand");
  *r_uv = GPU_attribute(mat, CD_AUTO_FROM_NAME, "");
}

/* -------------------------------------------------------------------- Ensō Hair Shadow */
namespace nodes::node_shader_enso_hair_shadow_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Map"_ustr)
      .default_value(0.5f)
      .hide_value()
      .description("Your own hair map (the green of an HM map). Unconnected, the node makes "
                   "the map itself from the hair strand data, with no image");
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({-0.4f, -0.5f, 0.75f})
      .description("Direction toward the light, for example the Direction of Ensō Light Select");
  b.add_input<decl::Float>("Map Strength"_ustr)
      .default_value(1.6f)
      .min(0.0f)
      .max(8.0f)
      .description("How far the strands pull the shadow edge into points. 0 is a plain edge");
  b.add_input<decl::Float>("Neutral Gray"_ustr)
      .default_value(0.29f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Map value that leaves the light as it is. Brighter parts of the map gain "
                   "light, darker parts lose it");
  b.add_input<decl::Float>("Shadow Bias"_ustr)
      .default_value(0.11f)
      .min(-1.0f)
      .max(1.0f)
      .description("Where the shadow starts. Higher makes the shadow bigger");
  b.add_input<decl::Float>("Softness"_ustr).default_value(0.06f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Float>("Variation"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How different the strands are from each other, so the points have "
                   "different lengths");
  b.add_input<decl::Float>("Detail"_ustr)
      .default_value(0.4f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Finer points inside every strand");
  b.add_input<decl::Float>("Strands"_ustr)
      .default_value(24.0f)
      .min(1.0f)
      .max(400.0f)
      .description("Only used on hair without strand data: the number of even strands the "
                   "active UV map is cut into along U");
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value()
      .description("Plug in Ensō Smooth Normals for one clean shadow over the whole hair");
  b.add_output<decl::Float>("Lit"_ustr).description("1 in the light, 0 in the shadow");
  b.add_output<decl::Float>("Strand Map"_ustr)
      .description("The hair map the node uses: a ridge across every strand");
  b.add_output<decl::Float>("Strand"_ustr)
      .description("Random value per strand, for color variation");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  const float use_map = in[0].link ? 1.0f : 0.0f;
  if (!in[10].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[10].link);
  }
  GPUNodeLink *strand_uv, *strand_id, *uv;
  enso_hair_strand_links(mat, &strand_uv, &strand_id, &uv);
  const bool ok = GPU_stack_link(mat,
                                 node,
                                 "node_enso_hair_shadow",
                                 in,
                                 out,
                                 strand_uv,
                                 strand_id,
                                 uv,
                                 GPU_constant(&use_map));
  if (ok) {
    enso_pass_write(mat, "Enso Cel", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_hair_shadow_cc

void register_node_type_sh_enso_hair_shadow()
{
  namespace file_ns = nodes::node_shader_enso_hair_shadow_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoHairShadow"_ustr);
  ntype.ui_name = "Ensō Hair Shadow";
  ntype.ui_description =
      "Anime hair shadow that follows the light, with an edge that breaks into strand shaped "
      "points. The hair map is made from the hair strand data, no image needed (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 180.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Hair Highlight */
namespace nodes::node_shader_enso_hair_highlight_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Highlight Map"_ustr)
      .default_value(1.0f)
      .hide_value()
      .description("Your own painted highlights (the red of an HM map). Unconnected, the node "
                   "draws the highlight itself from the hair strand data");
  b.add_input<decl::Vector>("Light Direction"_ustr)
      .default_value({0.0f, -0.5f, 0.85f})
      .description("Direction toward the light, for example from Ensō Light Select");
  b.add_input<decl::Float>("Position"_ustr)
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Where the band sits, measured down from the top of the head: 0 is the "
                   "very top, 0.5 is level with the head centre");
  b.add_input<decl::Float>("Height"_ustr)
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How tall the band is");
  b.add_input<decl::Float>("Jagged"_ustr)
      .default_value(0.7f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How different the slivers are in length and in height. 0 gives an even "
                   "band");
  b.add_input<decl::Float>("Slivers"_ustr)
      .default_value(2.0f)
      .min(1.0f)
      .max(16.0f)
      .description("Number of slivers across one strand. Use 1 on hair made of many thin "
                   "strands");
  b.add_input<decl::Float>("Coverage"_ustr)
      .default_value(0.8f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How many of the slivers are drawn. Lower leaves gaps in the band");
  b.add_input<decl::Float>("Width"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(2.0f)
      .description("Width of the slivers. Lower leaves a gap between them");
  b.add_input<decl::Float>("Ring Size"_ustr)
      .default_value(0.6f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How far around the reflection of the light the band shows. 1 shows it "
                   "all around the head, all the time");
  b.add_input<decl::Float>("Ring Softness"_ustr)
      .default_value(0.25f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How gently the band fades at the edge of the ring");
  b.add_input<decl::Float>("Intensity"_ustr).default_value(1.0f).min(0.0f).max(10.0f);
  b.add_input<decl::Float>("View Shift"_ustr)
      .default_value(0.1f)
      .min(-1.0f)
      .max(1.0f)
      .description("How far the band slides down the hair as the camera goes up");
  b.add_input<decl::Float>("Along Strand"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 places the band by height on the head, so it lines up around it. 1 "
                   "places it by distance from the root of each strand");
  b.add_input<decl::Vector>("Center"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .subtype(PROP_XYZ)
      .description("Centre of the head in the object's own space. Left at zero, the centre "
                   "stored by Make Hair Strand Data is used");
  b.add_input<decl::Float>("Strands"_ustr)
      .default_value(24.0f)
      .min(1.0f)
      .max(400.0f)
      .description("Only used on hair without strand data: the number of even strands the "
                   "active UV map is cut into along U");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Blurs the edges of the slivers, for the soft painted look");
  b.add_input<decl::Float>("Edge Pixels"_ustr)
      .default_value(1.5f)
      .min(0.0f)
      .max(16.0f)
      .description("Width of the smoothed edge in screen pixels");
  b.add_input<decl::Vector>("Normal"_ustr)
      .hide_value()
      .description("Plug in Ensō Smooth Normals so the ring is one clean shape");
  b.add_output<decl::Float>("Highlight"_ustr)
      .description("The band, as bright as the ring lets it be");
  b.add_output<decl::Float>("Marks"_ustr).description("The band alone, always on");
  b.add_output<decl::Float>("Strand"_ustr)
      .description("Random value per strand, for color variation");
}

static void node_buts(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "style", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  const float use_map = in[0].link ? 1.0f : 0.0f;
  const float style = float(node->custom1);
  if (!in[17].link) {
    GPU_link(mat, "world_normals_get", GPU_shading_data(), &in[17].link);
  }
  GPUNodeLink *strand_uv, *strand_id, *uv;
  enso_hair_strand_links(mat, &strand_uv, &strand_id, &uv);
  /* Head centre stored on the object by Make Hair Strand Data. */
  float attr_hash = 0.0f;
  GPUNodeLink *center_raw = GPU_uniform_attribute(
      mat, "enso_hair_center", false, reinterpret_cast<uint32_t *>(&attr_hash));
  GPUNodeLink *center_prop = nullptr;
  GPU_link(mat,
           "node_attribute_uniform",
           center_raw,
           GPU_constant(&attr_hash),
           GPU_kernel_globals(),
           GPU_shading_data(),
           &center_prop);
  const bool ok = GPU_stack_link(mat,
                                 node,
                                 "node_enso_hair_highlight",
                                 in,
                                 out,
                                 strand_uv,
                                 strand_id,
                                 uv,
                                 center_prop,
                                 GPU_constant(&use_map),
                                 GPU_constant(&style),
                                 GPU_kernel_globals(),
                                 GPU_shading_data());
  if (ok) {
    enso_pass_write(mat, "Enso Hair Ring", out[0].link);
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_hair_highlight_cc

void register_node_type_sh_enso_hair_highlight()
{
  namespace file_ns = nodes::node_shader_enso_hair_highlight_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoHairHighlight"_ustr);
  ntype.ui_name = "Ensō Hair Highlight";
  ntype.ui_description =
      "The band of light across anime hair, drawn the way illustrators draw it: spikes, "
      "triangles, blocks, lines or a mix, cut along the strands. Made from the hair strand "
      "data, no image needed (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_buts;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 180.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Part Tags */
namespace nodes::node_shader_enso_part_tags_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Color"_ustr)
      .default_value({0.8f, 0.8f, 0.8f, 1.0f})
      .description("The flat colour of the surface, before any light. It goes to the Flat Color "
                   "pass and comes out again unchanged");
  auto tag = [&](UString name) {
    b.add_input<decl::Float>(name)
        .default_value(0.0f)
        .min(0.0f)
        .max(1.0f)
        .subtype(PROP_FACTOR)
        .description("1 where the surface is this part of the character. Set it to 1, or plug "
                     "in a mask when one material covers several parts");
  };
  tag("Face"_ustr);
  tag("Hair"_ustr);
  tag("Eye"_ustr);
  tag("Skin"_ustr);
  tag("Cloth"_ustr);
  b.add_output<decl::Color>("Color"_ustr);
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  const bool ok = GPU_stack_link(mat, node, "node_enso_part_tags", in, out);
  if (ok) {
    const char *passes[] = {"Enso Face", "Enso Hair", "Enso Eye", "Enso Skin", "Enso Cloth"};
    int i = 0;
    for (bNodeSocket &sock : node->inputs) {
      if (i == 0) {
        enso_pass_write_color(
            mat, "Enso Flat Color", in[0].link ? in[0].link : GPU_uniform(in[0]));
      }
      else if (i <= 5) {
        const float value = sock.default_value_typed<bNodeSocketValueFloat>()->value;
        if (in[i].link) {
          enso_pass_write(mat, passes[i - 1], in[i].link);
        }
        else if (value > 0.0f) {
          enso_pass_write(mat, passes[i - 1], GPU_uniform(in[i]));
        }
      }
      i++;
    }
  }
  return ok;
}

}  // namespace nodes::node_shader_enso_part_tags_cc

void register_node_type_sh_enso_part_tags()
{
  namespace file_ns = nodes::node_shader_enso_part_tags_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoPartTags"_ustr);
  ntype.ui_name = "Ensō Part Tags";
  ntype.ui_description =
      "Mark the surface as face, hair, eye, skin or cloth and give its flat colour: both go to "
      "render passes, as clean masks and an unlit paint layer for the compositor (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 160.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Eye Parallax */
namespace nodes::node_shader_enso_eye_parallax_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector"_ustr)
      .hide_value()
      .description("Texture coordinates of the iris, usually Object coordinates");
  b.add_input<decl::Vector>("Forward"_ustr)
      .default_value({0.0f, -1.0f, 0.0f})
      .description("Direction the eye looks, in the object's own space");
  b.add_input<decl::Float>("Depth"_ustr)
      .default_value(0.05f)
      .min(-1.0f)
      .max(1.0f)
      .description("How far behind the surface the iris sits");
  b.add_output<decl::Vector>("Vector"_ustr)
      .description("Shifted coordinates for the iris texture");
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  return GPU_stack_link(
      mat, node, "node_enso_eye_parallax", in, out, GPU_kernel_globals(), GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_eye_parallax_cc

void register_node_type_sh_enso_eye_parallax()
{
  namespace file_ns = nodes::node_shader_enso_eye_parallax_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoEyeParallax"_ustr);
  ntype.ui_name = "Ensō Eye Parallax";
  ntype.ui_description = "Makes a flat iris look set back inside the eye: the texture slides as the camera moves (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

/* -------------------------------------------------------------------- Ensō Light Select */
namespace nodes::node_shader_enso_light_select_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Vector>("Direction"_ustr)
      .description(
          "Direction toward the chosen light. Connect it to the Light Direction of Ensō Shade "
          "Info, Face Shadow, Hair Ring, Stylized Highlight or Light Rim");
  b.add_output<decl::Color>("Color"_ustr).description("Colour of the light");
  b.add_output<decl::Float>("Strength"_ustr).description("Power of the light");
  b.add_output<decl::Vector>("Position"_ustr).description("Where the light is");
  b.add_output<decl::Float>("Distance"_ustr).description("Distance from the surface to the light");
  b.add_output<decl::Float>("Shadow"_ustr)
      .description(
          "The light's real shadow: 1 where it reaches the surface, 0 where something blocks it. "
          "Connect it to the Mask of Ensō Shade Info. It comes from the light's shadow maps, so "
          "it stays the same from any view");
}

static void node_buts(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "object", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_LIGHT);
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  Object *ob = reinterpret_cast<Object *>(node->id);
  /* A zero matrix tells the shader that no light is chosen. */
  float no_matrix[4][4] = {};
  float white[3] = {1.0f, 1.0f, 1.0f};
  float power = 0.0f;
  float is_sun = 0.0f;
  if (ob && ob->type == OB_LAMP && ob->data) {
    const Light *la = id_cast<Light *>(ob->data);
    white[0] = la->r;
    white[1] = la->g;
    white[2] = la->b;
    power = la->energy;
    is_sun = (la->type == LA_SUN) ? 1.0f : 0.0f;
  }
  else if (ob) {
    /* Any other object works as a direction target: an empty you aim by hand. */
    power = 1.0f;
  }
  if (ob && ob->base_flag != 0) {
    /* A hidden light is off: the eye in the viewport, the camera in a render. */
    const int needed = G.is_rendering ? BASE_ENABLED_RENDER :
                                        BASE_ENABLED_AND_VISIBLE_IN_DEFAULT_VIEWPORT;
    if ((ob->base_flag & needed) == 0) {
      power = 0.0f;
    }
  }
  GPUNodeLink *matrix = ob ? GPU_uniform(&ob->object_to_world()[0][0]) :
                             GPU_uniform(&no_matrix[0][0]);
  /* The Shadow output reads the light's shadow maps, which needs the lighting data. */
  GPU_material_flag_set(mat, GPU_MATFLAG_LIGHTING);
  return GPU_stack_link(mat,
                        node,
                        "node_enso_light_select",
                        in,
                        out,
                        matrix,
                        GPU_uniform(white),
                        GPU_uniform(&power),
                        GPU_uniform(&is_sun),
                        GPU_kernel_globals(),
                        GPU_shading_data());
}

}  // namespace nodes::node_shader_enso_light_select_cc

void register_node_type_sh_enso_light_select()
{
  namespace file_ns = nodes::node_shader_enso_light_select_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeEnsoLightSelect"_ustr);
  ntype.ui_name = "Ensō Light Select";
  ntype.ui_description =
      "Pick one light. Its direction follows the light as you move or turn it, so the cel "
      "shading of this material answers to that light only (EEVEE)";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_buts;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.default_width = 170.0f;
  bke::node_register_type(ntype);
}

}  // namespace blender
