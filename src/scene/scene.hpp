#pragma once

#include "core/object.hpp"
#include "gpu/gpu.hpp"

#include "render.h"

// -----------------------------------------------------------------------------

struct scene_node;
struct scene_tree;
struct scene_transform;
struct scene_texture;
struct scene_input_plane;

struct scene_context;
CORE_OBJECT_EXPLICIT_DECLARE(scene_context);

auto scene_create(gpu_context*, struct io_context*) -> ref<scene_context>;

struct scene_scene
{
    scene_tree*      tree;
    scene_transform* transform;
};

enum class scene_layer
{
    background,
    normal,
    overlay,
};

auto scene_get_layer(scene_context*, scene_layer) -> scene_tree*;
auto scene_get_root_transform(scene_context*) -> scene_transform*;

// -----------------------------------------------------------------------------

struct scene_client;
CORE_OBJECT_EXPLICIT_DECLARE(scene_client);
auto scene_client_create(scene_context*) -> ref<scene_client>;

// -----------------------------------------------------------------------------

enum class scene_modifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

using scene_scancode = u32;

struct scene_keyboard;
struct scene_pointer;

auto scene_pointer_get_position(scene_context*) -> vec2f32;

auto scene_keyboard_get_modifiers(scene_context*) -> flags<scene_modifier>;
void scene_keyboard_grab(scene_client*);
// Clear client keyboard grab, defers to next most recent keyboard grab
void scene_keyboard_ungrab(scene_client*);
// Clear all keyboard grabs
void scene_keyboard_clear_focus(scene_context*);

// -----------------------------------------------------------------------------

enum class scene_node_type
{
    transform,
    tree,
    texture,
    mesh,
    input_plane,
};

struct scene_node : core_object
{
    scene_node_type      type;      // Node type
    scene_tree*          parent;    // Parent in the layer hierarchy, controls z-order and visibility
    ref<scene_transform> transform; // Parent in the transform hierarhcy, controls xy positioning

    ~scene_node();
};

void scene_node_unparent(     scene_node*);
void scene_node_set_transform(scene_node*, scene_transform*);

struct scene_transform_state
{
    vec2f32 translation;
    f32     scale;
};

struct scene_transform : scene_node
{
    scene_transform_state local;
    scene_transform_state global;

    std::vector<scene_node*> children;

    ~scene_transform();
};

auto scene_transform_create(scene_context*) -> ref<scene_transform>;
void scene_transform_update(scene_transform*, vec2f32 translation, f32 scale);
auto scene_transform_get_local( scene_transform*) -> scene_transform_state;
auto scene_transform_get_global(scene_transform*) -> scene_transform_state;

struct scene_tree : scene_node
{
    scene_context* ctx;

    std::vector<ref<scene_node>> children;

    ~scene_tree();
};

auto scene_tree_create(scene_context*) -> ref<scene_tree>;
void scene_tree_place_below(scene_tree*, scene_node* reference, scene_node* to_place);
void scene_tree_place_above(scene_tree*, scene_node* reference, scene_node* to_place);

struct scene_texture : scene_node
{
    ref<gpu_image>   image;
    ref<gpu_sampler> sampler;
    gpu_blend_mode   blend;

    vec4u8   tint;
    aabb2f32 src;
    rect2f32 dst;
};

auto scene_texture_create(scene_context*) -> ref<scene_texture>;
void scene_texture_set_image(scene_texture*, gpu_image*, gpu_sampler*, gpu_blend_mode);
void scene_texture_set_tint( scene_texture*, vec4u8   tint);
void scene_texture_set_src(  scene_texture*, aabb2f32 src);
void scene_texture_set_dst(  scene_texture*, aabb2f32 dst);
void scene_texture_damage(   scene_texture*, aabb2f32 damage);

struct scene_mesh : scene_node
{
    ref<gpu_image>   image;
    ref<gpu_sampler> sampler;
    gpu_blend_mode   blend;

    aabb2f32 clip;

    std::vector<scene_vertex> vertices;
    std::vector<u16>         indices;
};

auto scene_mesh_create(scene_context*) -> ref<scene_mesh>;
void scene_mesh_update(scene_mesh*, gpu_image*, gpu_sampler*, gpu_blend_mode, aabb2f32 clip, std::span<const scene_vertex> vertices, std::span<const u16> indices);

struct scene_input_plane : scene_node
{
    scene_client* client;
    aabb2f32     rect;   // input region in transform-local space

    ~scene_input_plane();
};

auto scene_input_plane_create(scene_client*) -> ref<scene_input_plane>;
void scene_input_plane_set_rect(scene_input_plane*, aabb2f32);

// -----------------------------------------------------------------------------

// Represents a normal interactable "toplevel" window.
struct scene_window;
CORE_OBJECT_EXPLICIT_DECLARE(scene_window);

auto scene_window_create(scene_client*) -> ref<scene_window>;
// Adds the window to the UI scene. In response to this event
// the window may be repositioned and/or resized to fit in layout.
void scene_window_map(scene_window*);
// Removes the window from the scene.
void scene_window_unmap(scene_window*);
// Sets the window frame size for decorations and layout placement.
void scene_window_set_size(scene_window*, vec2u32);
// Get the window tree, this is used to attach window contents to
auto scene_window_get_tree(scene_window*) -> scene_tree*;
// Get the window transform, this is used to anchor window contents to
auto scene_window_get_transform(scene_window*) -> scene_transform*;

// -----------------------------------------------------------------------------

enum class scene_event_type
{
    keyboard_key,
    keyboard_modifier,

    pointer_motion,
    pointer_button,
    pointer_scroll,

    focus_keyboard,
    focus_pointer,

    window_resize,
};

struct scene_keyboard_event
{
    scene_scancode code;
    xkb_keysym_t sym;
    const char* utf8;
    bool pressed;
    bool quiet;
};

struct scene_pointer_event
{
    vec2f32 delta;
    scene_scancode button;
    bool pressed;
    bool quiet;
};

struct scene_focus
{
    scene_client*      client;
    scene_input_plane* plane;
};

struct scene_focus_event
{
    scene_focus lost;
    scene_focus gained;
};

struct scene_window_event
{
    scene_window* window;
    union {
        vec2u32 resize;
    };
};

struct scene_event
{
    scene_event_type type;

    union {
        scene_window_event   window;
        scene_keyboard_event key;
        scene_pointer_event  pointer;
        scene_focus_event    focus;
    };
};

using scene_event_handler_fn = void(scene_event*);

void scene_client_set_event_handler(scene_client*, std::move_only_function<scene_event_handler_fn>&&);
