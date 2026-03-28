#include "render.h"

[vk::push_constant] SceneRenderInput si;

struct VertexOutput
{
    vec4f32 pos : SV_Position;
    vec2f32 uv;
    vec4f32 color;
};

[shader("vertex")]
VertexOutput vertex(u32 id : SV_VulkanVertexID)
{
    var v = si.vertices[id];
    VertexOutput out;
    out.pos = vec4f32(fma(v.pos, si.scale, si.offset), 0, 1);
    out.uv = v.uv;
    out.color = unpack_unorm4u8(v.color);
    return out;
}

[shader("fragment")]
vec4f32 fragment(in VertexOutput vout)
{
    return si.texture.sample(vout.uv) * vout.color;
}
