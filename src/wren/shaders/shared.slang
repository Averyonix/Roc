#pragma once

#define WREN_SHADER 1

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float  f32;
typedef double f64;

struct vec4u8 {
    u8 x, y, z, w;
};

typedef float2 vec2f32;
typedef int2   vec2i32;
typedef uint2  vec2u32;

typedef float3 vec3f32;
typedef int3   vec3i32;
typedef uint3  vec3u32;

typedef float4 vec4f32;
typedef int4   vec4i32;
typedef uint4  vec4u32;

typedef float2x2 mat2x2f32;
typedef float2x3 mat2x3f32;

typedef float3x2 mat3x2f32;
typedef float3x3 mat3x3f32;
typedef float3x4 mat3x4f32;

typedef float4x3 mat4x3f32;
typedef float4x4 mat4x4f32;

struct rect2i32 { vec2i32 origin; vec2i32 extent; };
struct rect2f32 { vec2f32 origin; vec2f32 extent; };

// -----------------------------------------------------------------------------

[[vk::binding(0, 0)]] __DynamicResource<__DynamicResourceKind.General> wren_heap_texture[];
[[vk::binding(1, 0)]] __DynamicResource<__DynamicResourceKind.Sampler> wren_heap_sampler[];

struct wren_image_handle<T : ITexelElement>
{
    u32 image   : 20;
    u32 sampler : 12;

    __init()
    {
        this.image = 0;
    }

    __init(u32 image)
    {
        this.image = image;
    }

    __init(u32 image, u32 sampler)
    {
        this.image = image;
        this.sampler = sampler;
    }

    vec2u32 get_dimensions()
    {
        u32 width, height;
        wren_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>().GetDimensions(width, height);

        return vec2u32(width, height);
    }

    T sample(vec2f32 uv)
    {
        return wren_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()
            .Sample(wren_heap_sampler[NonUniformResourceIndex(sampler)].as<SamplerState>(), uv);
    }

    T sample_lod(vec2f32 uv, u32 lod)
    {
        return wren_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()
            .SampleLevel(wren_heap_sampler[NonUniformResourceIndex(sampler)].as<SamplerState>(), uv, lod);
    }

    T load(vec2u32 idx)
    {
        return wren_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()[idx];
    }
}
