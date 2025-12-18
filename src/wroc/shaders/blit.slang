#include "blit.h"

static const vec2f32 uvs[6] = { { 0, 0 }, { 0, 1 }, { 1, 0 },
                                { 0, 1 }, { 1, 1 }, { 1, 0 } };

[vk::push_constant] wroc_shader_rect_input si;

struct vertex_output
{
    u32 rect_id;
    vec4f32 position : SV_Position;
    vec2f32 tex;
};

[shader("vertex")]
vertex_output vertex(u32 raw_id: SV_VulkanVertexID)
{
    u32 rect_id = raw_id / 6;
    u32 id      = raw_id % 6;
    wroc_shader_rect rect = si.rects[rect_id];

    vertex_output out = {};
    out.rect_id = rect_id;
    out.tex      = uvs[id] * rect.image_rect.extent + rect.image_rect.origin;
    vec2f32 pos  = uvs[id] * rect.rect.extent       + rect.rect.origin;
    out.position = vec4f32(pos * 2 / si.output_size - vec2f32(1), 0, 1);

    return out;
}

[shader("fragment")]
vec4f32 fragment(in vertex_output vout)
{
    wroc_shader_rect rect = si.rects[vout.rect_id];

    vec2f32 uv = vout.tex / rect.image.get_dimensions();
    vec4f32 color = rect.image.sample(uv);

    if (rect.image_has_alpha == 0) color.w = 1;
    if (rect.opacity < 1.0) color *= rect.opacity;

    // if (color.w < 1) discard;
    return color;
}
