#pragma once

#include "wrei/object.hpp"
#include "wren/wren.hpp"

#include "render.h"

// -----------------------------------------------------------------------------

struct wrui_node;
struct wrui_tree;
struct wrui_transform;
struct wrui_texture;
struct wrui_input_plane;

struct wrui_context;
WREI_OBJECT_EXPLICIT_DECLARE(wrui_context);

auto wrui_create(wren_context*, struct wrio_context*) -> ref<wrui_context>;

struct wrui_scene
{
    wrui_tree*      tree;
    wrui_transform* transform;
};

enum class wrui_layer
{
    background,
    normal,
    overlay,
};

auto wrui_get_layer(wrui_context*, wrui_layer) -> wrui_tree*;
auto wrui_get_root_transform(wrui_context*) -> wrui_transform*;

// -----------------------------------------------------------------------------

struct wrui_client;
WREI_OBJECT_EXPLICIT_DECLARE(wrui_client);
auto wrui_client_create(wrui_context*) -> ref<wrui_client>;

// -----------------------------------------------------------------------------

enum class wrui_modifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

using wrui_scancode = u32;

struct wrui_keyboard;
struct wrui_pointer;

auto wrui_pointer_get_position(wrui_context*) -> vec2f32;

auto wrui_keyboard_get_modifiers(wrui_context*) -> flags<wrui_modifier>;
void wrui_keyboard_grab(wrui_client*);
// Clear client keyboard grab, defers to next most recent keyboard grab
void wrui_keyboard_ungrab(wrui_client*);
// Clear all keyboard grabs
void wrui_keyboard_clear_focus(wrui_context*);

// -----------------------------------------------------------------------------

enum class wrui_node_type
{
    transform,
    tree,
    texture,
    mesh,
    input_plane,
};

struct wrui_node : wrei_object
{
    wrui_node_type      type;      // Node type
    wrui_tree*          parent;    // Parent in the layer hierarchy, controls z-order and visibility
    ref<wrui_transform> transform; // Parent in the transform hierarhcy, controls xy positioning

    ~wrui_node();
};

void wrui_node_unparent(     wrui_node*);
void wrui_node_set_transform(wrui_node*, wrui_transform*);

struct wrui_transform_state
{
    vec2f32 translation;
    f32     scale;
};

struct wrui_transform : wrui_node
{
    wrui_transform_state local;
    wrui_transform_state global;

    std::vector<wrui_node*> children;

    ~wrui_transform();
};

auto wrui_transform_create(wrui_context*) -> ref<wrui_transform>;
void wrui_transform_update(wrui_transform*, vec2f32 translation, f32 scale);
auto wrui_transform_get_local( wrui_transform*) -> wrui_transform_state;
auto wrui_transform_get_global(wrui_transform*) -> wrui_transform_state;

struct wrui_tree : wrui_node
{
    wrui_context* ctx;

    std::vector<ref<wrui_node>> children;

    ~wrui_tree();
};

auto wrui_tree_create(wrui_context*) -> ref<wrui_tree>;
void wrui_tree_place_below(wrui_tree*, wrui_node* reference, wrui_node* to_place);
void wrui_tree_place_above(wrui_tree*, wrui_node* reference, wrui_node* to_place);

struct wrui_texture : wrui_node
{
    ref<wren_image>   image;
    ref<wren_sampler> sampler;
    wren_blend_mode   blend;

    vec4u8   tint;
    aabb2f32 src;
    rect2f32 dst;
};

auto wrui_texture_create(wrui_context*) -> ref<wrui_texture>;
void wrui_texture_set_image(wrui_texture*, wren_image*, wren_sampler*, wren_blend_mode);
void wrui_texture_set_tint( wrui_texture*, vec4u8   tint);
void wrui_texture_set_src(  wrui_texture*, aabb2f32 src);
void wrui_texture_set_dst(  wrui_texture*, aabb2f32 dst);
void wrui_texture_damage(   wrui_texture*, aabb2f32 damage);

struct wrui_mesh : wrui_node
{
    ref<wren_image>   image;
    ref<wren_sampler> sampler;
    wren_blend_mode   blend;

    aabb2f32 clip;

    std::vector<wrui_vertex> vertices;
    std::vector<u16>         indices;
};

auto wrui_mesh_create(wrui_context*) -> ref<wrui_mesh>;
void wrui_mesh_update(wrui_mesh*, wren_image*, wren_sampler*, wren_blend_mode, aabb2f32 clip, std::span<const wrui_vertex> vertices, std::span<const u16> indices);

struct wrui_input_plane : wrui_node
{
    wrui_client* client;
    aabb2f32     rect;   // input region in transform-local space

    ~wrui_input_plane();
};

auto wrui_input_plane_create(wrui_client*) -> ref<wrui_input_plane>;
void wrui_input_plane_set_rect(wrui_input_plane*, aabb2f32);

// -----------------------------------------------------------------------------

// Represents a normal interactable "toplevel" window.
struct wrui_window;
WREI_OBJECT_EXPLICIT_DECLARE(wrui_window);

auto wrui_window_create(wrui_client*) -> ref<wrui_window>;
// Adds the window to the UI scene. In response to this event
// the window may be repositioned and/or resized to fit in layout.
void wrui_window_map(wrui_window*);
// Removes the window from the scene.
void wrui_window_unmap(wrui_window*);
// Sets the window frame size for decorations and layout placement.
void wrui_window_set_size(wrui_window*, vec2u32);
// Get the window tree, this is used to attach window contents to
auto wrui_window_get_tree(wrui_window*) -> wrui_tree*;
// Get the window transform, this is used to anchor window contents to
auto wrui_window_get_transform(wrui_window*) -> wrui_transform*;

// -----------------------------------------------------------------------------

enum class wrui_event_type
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

struct wrui_keyboard_event
{
    wrui_scancode code;
    xkb_keysym_t sym;
    const char* utf8;
    bool pressed;
    bool quiet;
};

struct wrui_pointer_event
{
    vec2f32 delta;
    wrui_scancode button;
    bool pressed;
    bool quiet;
};

struct wrui_focus
{
    wrui_client*      client;
    wrui_input_plane* plane;
};

struct wrui_focus_event
{
    wrui_focus lost;
    wrui_focus gained;
};

struct wrui_window_event
{
    wrui_window* window;
    union {
        vec2u32 resize;
    };
};

struct wrui_event
{
    wrui_event_type type;

    union {
        wrui_window_event   window;
        wrui_keyboard_event key;
        wrui_pointer_event  pointer;
        wrui_focus_event    focus;
    };
};

using wrui_event_handler_fn = void(wrui_event*);

void wrui_client_set_event_handler(wrui_client*, std::move_only_function<wrui_event_handler_fn>&&);
