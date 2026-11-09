/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup cmpnodes
 *
 * Ensō Outline: anime ink lines in the compositor from the Depth and Normal passes, in one node
 * (planar-exact depth edges and Sobel normal edges, with a soft threshold), drawn over the image in the line color.
 */

#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"

#include "GPU_shader.hh"

#include "COM_node_operation.hh"
#include "COM_utilities.hh"

#include "node_composite_util.hh"

namespace blender::nodes::node_composite_enso_outline_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Image"_ustr)
      .default_value({1.0f, 1.0f, 1.0f, 1.0f})
      .hide_value()
      .structure_type(StructureType::Dynamic)
      .description("Image to draw the lines over");
  b.add_input<decl::Float>("Depth"_ustr)
      .default_value(1.0f)
      .hide_value()
      .structure_type(StructureType::Dynamic)
      .description("Depth pass of the render layer: outlines and depth steps");
  b.add_input<decl::Vector>("Normal"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .hide_value()
      .structure_type(StructureType::Dynamic)
      .description("Normal pass of the render layer: creases and folds");
  b.add_input<decl::Int>("Width"_ustr)
      .default_value(1)
      .min(1)
      .max(32)
      .subtype(PROP_PIXEL)
      .description("Line width in pixels");
  b.add_input<decl::Float>("Depth Threshold"_ustr)
      .default_value(0.05f)
      .min(0.0f)
      .max(10.0f)
      .description("Relative depth step that makes a line (lower draws more lines)");
  b.add_input<decl::Float>("Normal Threshold"_ustr)
      .default_value(0.25f)
      .min(0.0f)
      .max(2.0f)
      .description("Normal change that makes a line (lower draws more creases)");
  b.add_input<decl::Float>("Softness"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(4.0f)
      .description("Softness of the line edges");
  b.add_input<decl::Color>("Color"_ustr)
      .default_value({0.02f, 0.015f, 0.01f, 1.0f})
      .description("Ink color of the lines (its alpha is the line opacity)");

  b.add_output<decl::Color>("Image"_ustr).structure_type(StructureType::Dynamic);
  b.add_output<decl::Float>("Lines"_ustr).structure_type(StructureType::Dynamic);
}

using namespace blender::compositor;

class EnsoOutlineOperation : public NodeOperation {
 public:
  using NodeOperation::NodeOperation;

  void execute() override
  {
    const Result &image = this->get_input("Image");
    const bool use_depth = !this->get_input("Depth").is_single_value();
    const bool use_normal = !this->get_input("Normal").is_single_value();
    if (image.is_single_value() && !use_depth && !use_normal) {
      Result &out = this->get_result("Image");
      if (out.should_compute()) {
        out.share_data(image);
      }
      Result &lines = this->get_result("Lines");
      if (lines.should_compute()) {
        lines.allocate_single_value();
        lines.set_single_value(0.0f);
      }
      return;
    }
    if (this->context().use_gpu()) {
      if (this->get_result("Image").should_compute()) {
        this->execute_gpu("compositor_enso_outline_image", "Image", "output_img");
      }
      if (this->get_result("Lines").should_compute()) {
        this->execute_gpu("compositor_enso_outline_lines", "Lines", "lines_img");
      }
    }
    else {
      this->execute_cpu();
    }
  }

  void execute_gpu(const char *shader_name, const char *result_name, const char *image_name)
  {
    gpu::Shader *shader = this->context().get_shader(shader_name);
    GPU_shader_bind(shader);
    GPU_shader_uniform_1i(shader, "width", this->width());
    GPU_shader_uniform_1f(shader, "depth_threshold", this->input_float("Depth Threshold"));
    GPU_shader_uniform_1f(shader, "normal_threshold", this->input_float("Normal Threshold"));
    GPU_shader_uniform_1f(shader, "softness", this->input_float("Softness"));
    GPU_shader_uniform_1b(shader, "use_depth", !this->get_input("Depth").is_single_value());
    GPU_shader_uniform_1b(shader, "use_normal", !this->get_input("Normal").is_single_value());
    GPU_shader_uniform_4fv(shader, "line_color", this->line_color());

    const Result &image = this->get_input("Image");
    gpu::Texture *image_texture = image.bind_as_texture_or_single_value(shader, "image_tx");
    const Result &depth = this->get_input("Depth");
    gpu::Texture *depth_texture = depth.bind_as_texture_or_single_value(shader, "depth_tx");
    const Result &normal = this->get_input("Normal");
    gpu::Texture *normal_texture = normal.bind_as_texture_or_single_value(shader, "normal_tx");

    Result &output = this->get_result(result_name);
    const Domain domain = this->compute_domain();
    output.allocate_texture(domain);
    output.bind_as_image(shader, image_name);

    compute_dispatch_threads_at_least(shader, domain.data_size);

    image.unbind_as_texture_or_single_value(image_texture);
    depth.unbind_as_texture_or_single_value(depth_texture);
    normal.unbind_as_texture_or_single_value(normal_texture);
    output.unbind_as_image();
    GPU_shader_unbind();
  }

  static float edge(const float g, const float threshold, const float softness)
  {
    const float t = math::max(threshold, 1e-5f);
    const float s = math::max(softness, 0.0f) * t;
    const float lo = t - s * 0.5f;
    const float hi = t + s + 1e-5f;
    const float x = math::clamp((g - lo) / (hi - lo), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
  }

  void execute_cpu()
  {
    const Result &image = this->get_input("Image");
    const Result &depth = this->get_input("Depth");
    const Result &normal = this->get_input("Normal");
    const bool use_depth = !depth.is_single_value();
    const bool use_normal = !normal.is_single_value();
    const int w = this->width();
    const float depth_threshold = this->input_float("Depth Threshold");
    const float normal_threshold = this->input_float("Normal Threshold");
    const float softness = this->input_float("Softness");
    const float4 color = this->line_color();

    const Domain domain = this->compute_domain();
    const int2 size = domain.data_size;
    auto clamp_texel = [&](const int2 t) {
      return int2(math::clamp(t.x, 0, size.x - 1), math::clamp(t.y, 0, size.y - 1));
    };

    auto lines_at = [&](const int2 texel) {
      float lines = 0.0f;
      if (use_depth) {
        /* Planar-exact: second difference of inverse depth, zero on any flat surface. */
        auto inv = [&](const int2 t) {
          return 1.0f / math::max(depth.load_pixel<float, true>(clamp_texel(t)), 1e-6f);
        };
        const float inv0 = inv(texel);
        const int2 dirs[4] = {int2(1, 0), int2(0, 1), int2(1, 1), int2(1, -1)};
        float s = 0.0f;
        for (const int2 o : dirs) {
          s = math::max(s, (2.0f * inv0 - inv(texel + o * w) - inv(texel - o * w)) / inv0);
        }
        lines = math::max(lines, edge(s, depth_threshold, softness));
      }
      if (use_normal) {
        float3 gx(0.0f);
        float3 gy(0.0f);
        for (int y = -1; y <= 1; y++) {
          for (int x = -1; x <= 1; x++) {
            const float3 n = normal.load_pixel<float3, true>(clamp_texel(texel + int2(x, y) * w));
            gx += n * float(x) * (y == 0 ? 2.0f : 1.0f);
            gy += n * float(y) * (x == 0 ? 2.0f : 1.0f);
          }
        }
        const float g = math::sqrt(math::dot(gx, gx) + math::dot(gy, gy)) * 0.125f;
        lines = math::max(lines, edge(g, normal_threshold, softness));
      }
      return lines;
    };

    Result &output = this->get_result("Image");
    if (output.should_compute()) {
      output.allocate_texture(domain);
      parallel_for(size, [&](const int2 texel) {
        const float4 c = float4(image.load_pixel<Color, true>(texel));
        const float k = lines_at(texel) * color.w;
        const float3 rgb = math::interpolate(c.xyz(), color.xyz(), k);
        output.store_pixel(texel, Color(float4(rgb, math::max(c.w, k))));
      });
    }
    Result &lines_out = this->get_result("Lines");
    if (lines_out.should_compute()) {
      lines_out.allocate_texture(domain);
      parallel_for(size, [&](const int2 texel) { lines_out.store_pixel(texel, lines_at(texel)); });
    }
  }

  int width()
  {
    return math::clamp(this->get_input("Width").get_single_value_default<int>(), 1, 32);
  }

  float input_float(const char *name)
  {
    return this->get_input(name).get_single_value_default<float>();
  }

  float4 line_color()
  {
    return float4(this->get_input("Color").get_single_value_default<Color>());
  }
};

static NodeOperation *get_compositor_operation(Context &context, const bNode &node)
{
  return new EnsoOutlineOperation(context, node);
}

static void node_register()
{
  static bke::bNodeType ntype;

  cmp_node_type_base(&ntype, "CompositorNodeEnsoOutline"_ustr);
  ntype.ui_name = "Ensō Outline";
  ntype.ui_description =
      "Anime ink lines from the Depth and Normal passes, drawn over the image in one node";
  ntype.nclass = NODE_CLASS_OP_FILTER;
  ntype.declare = node_declare;
  ntype.get_compositor_operation = get_compositor_operation;

  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_composite_enso_outline_cc
