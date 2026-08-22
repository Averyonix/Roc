#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneRenderPixelPassInput pc; };

layout(local_size_x = SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE,
       local_size_y = SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    // Fetch bin

    const u32 groups_per_coarse_bin = SCENE_RENDER_COARSE_BIN_SIZE / SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE;
    const u32 coarse_bin_index = (gl_WorkGroupID.y / groups_per_coarse_bin) * pc.coarse_bin_row_stride
                               + (gl_WorkGroupID.x / groups_per_coarse_bin)
                               +  SCENE_RENDER_RESERVED_COARSE_BIN_COUNT;

    GPU_CONST_PTR(SceneRenderCoarseBinInfo) coarse_bin_info = pc.coarse_bin_infos[coarse_bin_index];
    const u32 max_depth = coarse_bin_info._.depth;

    // Compute fine bin input locations

    const u32 groups_per_fine_bin = SCENE_RENDER_FINE_BIN_SIZE / SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE;
    const u32 fine_bin_index = (((gl_WorkGroupID.y / groups_per_fine_bin) % SCENE_RENDER_COARSE_FINE_BIN_RATIO) * SCENE_RENDER_COARSE_FINE_BIN_RATIO)
                              + ((gl_WorkGroupID.x / groups_per_fine_bin) % SCENE_RENDER_COARSE_FINE_BIN_RATIO);

    GPU_CONST_PTR(u16) fine_bin = pc.fine_bins[coarse_bin_info._.offset + fine_bin_index * coarse_bin_info._.depth];

    // Evaluate

    vec4f32 color = vec4f32(0, 0, 0, 0);

    const vec2f32 pos = vec2f32(gl_GlobalInvocationID.xy);

    for (u32 i = 0; i < max_depth; ++i) {
        const u32 quad_id = fine_bin[i]._;
        if (quad_id == 0) break;

        const aabb2f32 bounds = pc.quad_bounds[quad_id]._;
        if (       pos.x >= bounds.min.x && pos.x < bounds.max.x
                && pos.y >= bounds.min.y && pos.y < bounds.max.y) {
            GPU_CONST_PTR(SceneRenderQuad) quad = pc.quads[quad_id];

            vec2f32 uv = (pos + vec2f32(0.5) - quad._.dst.origin) / quad._.dst.extent;
                    uv = fma(uv, quad._.src.extent, quad._.src.origin);

            // Blend
            color += quad_sample(quad, uv) * (1.f - color.a);

            // Early stop
            if (color.a >= 1) break;
        }
    }

    // Store

    color.rgb = srgb_oetf(color.rgb);

    gpu_image_store(pc.target, vec2i32(gl_GlobalInvocationID.xy), color);
}
