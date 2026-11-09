/* SPDX-FileCopyrightText: 2026 Mix 3D Design
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"
#endif

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(compositor_enso_outline_shared)
LOCAL_GROUP_SIZE(16, 16)
PUSH_CONSTANT(int, width)
PUSH_CONSTANT(float, depth_threshold)
PUSH_CONSTANT(float, normal_threshold)
PUSH_CONSTANT(float, softness)
PUSH_CONSTANT(bool, use_depth)
PUSH_CONSTANT(bool, use_normal)
PUSH_CONSTANT(float4, line_color)
SAMPLER(0, sampler2D, image_tx)
SAMPLER(1, sampler2D, depth_tx)
SAMPLER(2, sampler2D, normal_tx)
COMPUTE_SOURCE("compositor_enso_outline.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(compositor_enso_outline_image)
DEFINE("WRITE_IMAGE")
IMAGE(0, SFLOAT_16_16_16_16, write, image2D, output_img)
ADDITIONAL_INFO(compositor_enso_outline_shared)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(compositor_enso_outline_lines)
IMAGE(0, SFLOAT_16, write, image2D, lines_img)
ADDITIONAL_INFO(compositor_enso_outline_shared)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
