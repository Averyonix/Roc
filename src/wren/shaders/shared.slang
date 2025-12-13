#pragma once

#define WREN_SHADER 1

// -----------------------------------------------------------------------------

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float  f32;
typedef double f64;

// -----------------------------------------------------------------------------

typedef vector<f32, 2> vec2f32;
typedef vector<i32, 2> vec2i32;
typedef vector<u32, 2> vec2u32;

typedef vector<f32, 3> vec3f32;
typedef vector<i32, 3> vec3i32;
typedef vector<u32, 3> vec3u32;

typedef vector<u8,  4> vec2u8;
typedef vector<f32, 4> vec4f32;
typedef vector<i32, 4> vec4i32;
typedef vector<u32, 4> vec4u32;

// -----------------------------------------------------------------------------

typedef matrix<f32, 2, 2> mat2x2f32;
typedef matrix<f32, 2, 3> mat2x3f32;

typedef matrix<f32, 3, 2> mat3x2f32;
typedef matrix<f32, 3, 3> mat3x3f32;
typedef matrix<f32, 3, 4> mat3x4f32;

typedef matrix<f32, 4, 3> mat4x3f32;
typedef matrix<f32, 4, 4> mat4x4f32;

// -----------------------------------------------------------------------------

struct wren_rect2i32<T>
{
    vector<T, 2> origin;
    vector<T, 2> extent;
};

typedef wren_rect2i32<i32> rect2i32;
typedef wren_rect2i32<f32> rect2f32;

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
};

typedef wren_image_handle<vec4f32> image4f32;
