/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Ensō Anime Sky: the Anime Sky Pro world shader compiled to native EEVEE GPU code. The BSL
 * library files are generated from the add-on's node graph by
 * dlc_dev/native_sky/gen_native_sky.py; this file by gen_node_cc.py. The images the sky samples
 * are given to the node by the Ensō Anime Sky add-on as node properties ("enso_image_<i>" with
 * "enso_use_<i>", "enso_ext_<i>", "enso_interp_<i>"), and are sampled exactly like the Image
 * Texture node samples them. EEVEE only; Cycles keeps the original node group.
 */

#include <algorithm>
#include <string>

#include "BKE_idprop.hh"

#include "DNA_image_types.h"

#include "IMB_colormanagement.hh"

#include "NOD_rna_define.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_enso_sky_cc {

static const EnumPropertyItem variant_items[] = {
    {0, "C12", 0, "Painted, Fast", ""},
    {1, "C20", 0, "Painted, Balanced", ""},
    {2, "C32", 0, "Painted, Fine", ""},
    {3, "P12", 0, "Procedural Tower, Fast", ""},
    {4, "P20", 0, "Procedural Tower, Balanced", ""},
    {5, "P32", 0, "Procedural Tower, Fine", ""},
    {0, nullptr, 0, nullptr, nullptr},
};
static const char *function_names[] = {"node_enso_sky_c12",
                                       "node_enso_sky_c20",
                                       "node_enso_sky_c32",
                                       "node_enso_sky_p12",
                                       "node_enso_sky_p20",
                                       "node_enso_sky_p32"};
/* Number of image samplers each generated function takes. */
static const int image_counts[] = {18, 20, 28, 3, 3, 3};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>(UString(StringRef("Zenith")), "in_0"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Mid sky")), "in_1"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Horizon")), "in_2"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Cloud light")), "in_3"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Cloud shadow")), "in_4"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Sun color")), "in_5"_ustr);
  b.add_input<decl::Vector>(UString(StringRef("Sun direction")), "in_6"_ustr);
  b.add_input<decl::Vector>(UString(StringRef("Offset")), "in_7"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Coverage")), "in_8"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Cloud size")), "in_9"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Altitude")), "in_10"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Density")), "in_11"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Erosion")), "in_12"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Contrast")), "in_13"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Edge definition")), "in_14"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Silver lining")), "in_15"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Haze")), "in_16"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Wisps")), "in_17"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Horizon banks")), "in_18"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Sky strength")), "in_19"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Sun diameter")), "in_20"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Sun glow")), "in_21"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Stars")), "in_22"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Curvature radius")), "in_23"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Parallax")), "in_24"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Relief")), "in_25"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Rotation")), "in_26"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon")), "in_27"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon phase")), "in_28"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Small clouds")), "in_29"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Shadow softness")), "in_30"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Openings")), "in_31"_ustr);
  b.add_input<decl::Vector>(UString(StringRef("Moon direction")), "in_32"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Moon color")), "in_33"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon diameter")), "in_34"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon visibility")), "in_35"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Sun visibility")), "in_36"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Twilight")), "in_37"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Daylight")), "in_38"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Night")), "in_39"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Solar horizon")), "in_40"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Cloud ambient")), "in_41"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Body shading")), "in_42"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Ambient strength")), "in_43"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Gradient motion")), "in_44"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Gradient height")), "in_45"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Gradient spread")), "in_46"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower amount")), "in_47"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower rotation")), "in_48"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower elevation")), "in_49"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower width")), "in_50"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower height")), "in_51"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower companions")), "in_52"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower drift")), "in_53"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower seed")), "in_54"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower fullness")), "in_55"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower billow scale")), "in_56"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower billow strength")), "in_57"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Tower crown")), "in_58"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Custom moon")), "in_59"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon image rotation")), "in_60"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Moon image zoom")), "in_61"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Custom clouds")), "in_62"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Custom companions")), "in_63"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Cloud image lighting")), "in_64"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Cloud image exposure")), "in_65"_ustr);
  b.add_input<decl::Color>(UString(StringRef("Cloud image tint")), "in_66"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Evolution")), "in_67"_ustr);
  b.add_input<decl::Float>(UString(StringRef("Evolution amount")), "in_68"_ustr);
  b.add_input<decl::Vector>("Direction"_ustr).hide_value();
  b.add_input<decl::Vector>("Parallax Point"_ustr).hide_value();
  b.add_input<decl::Float>("Is Camera Ray"_ustr).default_value(1.0f);
  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Strength"_ustr);
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "variant", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  node->custom1 = 0;
}

static IDProperty *node_prop(const bNode *node, const std::string &name)
{
  return node->prop ? IDP_GetPropertyFromGroup(node->prop, name) : nullptr;
}

static int node_prop_int(const bNode *node, const std::string &name, const int fallback)
{
  IDProperty *prop = node_prop(node, name);
  if (prop && prop->type == IDP_INT) {
    return IDP_int_get(prop);
  }
  if (prop && prop->type == IDP_BOOLEAN) {
    return IDP_bool_get(prop);
  }
  return fallback;
}

static Image *node_prop_image(const bNode *node, const std::string &name)
{
  IDProperty *prop = node_prop(node, name);
  if (prop && prop->type == IDP_ID) {
    ID *id = IDP_ID_get(prop);
    if (id && GS(id->name) == ID_IM) {
      return reinterpret_cast<Image *>(id);
    }
  }
  return nullptr;
}

/* Same sampler state as the Image Texture node. */
static GPUSamplerState sampler_state_get(const int extension, const int interpolation)
{
  GPUSamplerState state = GPUSamplerState::default_sampler();
  switch (extension) {
    case SHD_IMAGE_EXTENSION_EXTEND:
      state.extend_x = state.extend_yz = GPU_SAMPLER_EXTEND_MODE_EXTEND;
      break;
    case SHD_IMAGE_EXTENSION_REPEAT:
      state.extend_x = state.extend_yz = GPU_SAMPLER_EXTEND_MODE_REPEAT;
      break;
    case SHD_IMAGE_EXTENSION_CLIP:
      state.extend_x = state.extend_yz = GPU_SAMPLER_EXTEND_MODE_CLAMP_TO_BORDER;
      break;
    case SHD_IMAGE_EXTENSION_MIRROR:
      state.extend_x = state.extend_yz = GPU_SAMPLER_EXTEND_MODE_MIRRORED_REPEAT;
      break;
    default:
      break;
  }
  if (interpolation != SHD_INTERP_CLOSEST) {
    state.filtering = GPU_SAMPLER_FILTERING_ANISOTROPIC_ENABLE | GPU_SAMPLER_FILTERING_LINEAR |
                      GPU_SAMPLER_FILTERING_MIPMAP;
  }
  return state;
}

/* Same color/alpha output rules as the Image Texture node (see enso_sky_sample). */
static float sample_mode_get(const Image *ima, const int use)
{
  if (ima == nullptr) {
    return 0.0f;
  }
  const bool color_used = use & 1;
  const bool alpha_used = use & 2;
  if (!color_used) {
    return 4.0f;
  }
  if (ELEM(ima->alpha_mode, IMA_ALPHA_IGNORE, IMA_ALPHA_CHANNEL_PACKED) ||
      IMB_colormanagement_space_name_is_data(ima->colorspace_settings.name))
  {
    return 1.0f;
  }
  if (ima->alpha_mode == IMA_ALPHA_PREMUL) {
    return alpha_used ? 2.0f : 1.0f;
  }
  return alpha_used ? 1.0f : 3.0f;
}

static int node_gpu(GPUMaterial *mat,
                    bNode *node,
                    bNodeExecData * /*execdata*/,
                    GPUNodeStack *in,
                    GPUNodeStack *out)
{
  const int variant = std::clamp(int(node->custom1), 0, 5);
  const int count = image_counts[variant];

  /* A missing image still needs a sampler: borrow the first present one (it is not read). */
  Image *any = nullptr;
  for (int i = 0; i < count && !any; i++) {
    any = node_prop_image(node, "enso_image_" + std::to_string(i));
  }
  if (any == nullptr) {
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GPU_link(mat, "set_rgba", GPU_constant(zero), &out[0].link);
    GPU_link(mat, "set_value", GPU_constant(zero), &out[1].link);
    return true;
  }

  float luminance[3];
  IMB_colormanagement_get_luminance_coefficients(luminance);
  /* GPU_stack_link takes the extra links as varargs: build them in a fixed-size list. */
  GPUNodeLink *links[2 * 32 + 2] = {nullptr};
  float modes[32];
  int n = 0;
  links[n++] = GPU_constant(luminance);
  for (int i = 0; i < count; i++) {
    const std::string idx = std::to_string(i);
    Image *ima = node_prop_image(node, "enso_image_" + idx);
    const int use = node_prop_int(node, "enso_use_" + idx, 1);
    const int ext = node_prop_int(node, "enso_ext_" + idx, SHD_IMAGE_EXTENSION_CLIP);
    const int interp = node_prop_int(node, "enso_interp_" + idx, SHD_INTERP_LINEAR);
    modes[i] = sample_mode_get(ima, use);
    links[n++] = GPU_image(mat, ima ? ima : any, nullptr, sampler_state_get(ext, interp));
    links[n++] = GPU_constant(&modes[i]);
  }
  links[n++] = GPU_kernel_globals();
  return GPU_stack_link_list(mat, node, function_names[variant], in, out, links, n);
}

static void node_rna(StructRNA *srna)
{
  RNA_def_node_enum(srna,
                    "variant",
                    "Variant",
                    "Anime Sky quality variant",
                    variant_items,
                    NOD_inline_enum_accessors(custom1),
                    0);
}

}  // namespace nodes::node_shader_enso_sky_cc

void register_node_type_sh_enso_anime_sky()
{
  namespace file_ns = nodes::node_shader_enso_sky_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeEnsoAnimeSky"_ustr);
  ntype.ui_name = "Ensō Anime Sky";
  ntype.ui_description =
      "Anime Sky Pro world shader as native EEVEE code (used by the Ensō Anime Sky add-on)";
  ntype.nclass = NODE_CLASS_TEXTURE;
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_init;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.gpu_fn = file_ns::node_gpu;
  ntype.gather_link_search_ops = nullptr;
  ntype.default_width = 240.0f;
  bke::node_register_type(ntype);

  file_ns::node_rna(ntype.rna_ext.srna);
}

}  // namespace blender
