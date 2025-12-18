#include "imgui.h"

struct ImDrawVert
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

[vk::push_constant] wroc_imgui_shader_in pc;

struct vertex_output
{
    vec4f32 pos : SV_Position;
    vec2f32 uv;
    vec4f32 color;
};

[shader("vertex")]
vertex_output vertex(u32 id : SV_VulkanVertexID)
{
    var v = pc.vertices[id];
    vertex_output out;
    out.pos = vec4f32((v.pos * pc.scale) + pc.offset, 0, 1);
    out.uv = v.uv;
    out.color = wrei_unpack_unorm4u8(v.color);
    return out;
}

[shader("fragment")]
vec4f32 fragment(in vertex_output vout)
{
    return pc.texture.sample(vout.uv) * vout.color;
}
