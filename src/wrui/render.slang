#include "render.h"

[vk::push_constant] wrui_render_input si;

struct vertex_output
{
    vec4f32 pos : SV_Position;
    vec2f32 uv;
    vec4f32 color;
};

[shader("vertex")]
vertex_output vertex(u32 id : SV_VulkanVertexID)
{
    var v = si.vertices[id];
    vertex_output out;
    out.pos = vec4f32((v.pos * si.scale) + si.offset, 0, 1);
    out.uv = v.uv;
    out.color = wrei_unpack_unorm4u8(v.color);
    return out;
}

[shader("fragment")]
vec4f32 fragment(in vertex_output vout)
{
    return si.texture.sample(vout.uv) * vout.color;
}
