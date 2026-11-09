/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ensō Outline: planar-exact edges of the Depth pass (relative, so far and near objects get the
 * same lines) and Sobel edges of the Normal pass, turned into an ink line mask with a soft threshold. */

#include "infos/compositor_enso_outline_infos.hh"

#ifdef WRITE_IMAGE
COMPUTE_SHADER_CREATE_INFO(compositor_enso_outline_image)
#else
COMPUTE_SHADER_CREATE_INFO(compositor_enso_outline_lines)
#endif

#include "gpu_shader_compositor_texture_utilities.glsl"

float enso_edge(float g, float threshold)
{
  float t = max(threshold, 1e-5f);
  float s = max(softness, 0.0f) * t;
  return smoothstep(t - s * 0.5f, t + s + 1e-5f, g);
}

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  int w = max(width, 1);
  float lines = 0.0f;
  if (use_depth) {
    /* Planar-exact depth edges: inverse depth is linear across any flat surface in screen space,
     * so its second difference is zero on floors and water (even at grazing angles) and only
     * jumps where the surface really steps back. Only the nearer side of a step gets the line. */
    float inv0 = 1.0f / max(texture_load(depth_tx, texel).x, 1e-6f);
    float s = 0.0f;
    for (int i = 0; i < 4; i++) {
      int2 o = (i == 0) ? int2(1, 0) : (i == 1) ? int2(0, 1) : (i == 2) ? int2(1, 1) : int2(1, -1);
      float ia = 1.0f / max(texture_load(depth_tx, texel + o * w).x, 1e-6f);
      float ib = 1.0f / max(texture_load(depth_tx, texel - o * w).x, 1e-6f);
      s = max(s, (2.0f * inv0 - ia - ib) / inv0);
    }
    lines = max(lines, enso_edge(s, depth_threshold));
  }
  /* Sobel weights from the offset itself: x * (2 on the middle row), y * (2 on the middle
   * column). */
  if (use_normal) {
    float3 gx = float3(0.0f);
    float3 gy = float3(0.0f);
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        float3 n = texture_load(normal_tx, texel + int2(x, y) * w).xyz;
        gx += n * float(x) * (y == 0 ? 2.0f : 1.0f);
        gy += n * float(y) * (x == 0 ? 2.0f : 1.0f);
      }
    }
    float g = sqrt(dot(gx, gx) + dot(gy, gy)) * 0.125f;
    lines = max(lines, enso_edge(g, normal_threshold));
  }

#ifdef WRITE_IMAGE
  float4 color = texture_load(image_tx, texel);
  float k = lines * line_color.a;
  float4 result = float4(mix(color.rgb, line_color.rgb, k), max(color.a, k));
  imageStore(output_img, texel, result);
#else
  imageStore(lines_img, texel, float4(lines));
#endif
}
