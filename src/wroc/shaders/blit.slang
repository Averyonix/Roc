#include "blit.h"

static const vec2f32 uvs[6] = { { 0, 0 }, { 0, 1 }, { 1, 0 },
                                { 0, 1 }, { 1, 1 }, { 1, 0 } };

[vk::push_constant] wren_shader_surface_in si;

struct VertexOutput
{
    vec4f32 position : SV_Position;
    vec2f32 uv;
};

[shader("vertex")]
VertexOutput vertex(u32 id: SV_VertexID)
{
    VertexOutput out = {};
    out.uv       =         uvs[id] * si.src_extent + si.src_origin;
    out.position = vec4f32(uvs[id] * si.dst_extent + si.dst_origin, 0, 1);
    return out;
}

[shader("fragment")]
vec4f32 fragment(in VertexOutput vout)
{
    return si.image.sample(vout.uv);
}
