#pragma once

#include "wrei/object.hpp"
#include "wren/wren.hpp"

// -----------------------------------------------------------------------------

struct wrui_node;
struct wrui_tree;
struct wrui_transform;
struct wrui_texture;

struct wrui_context;
WREI_OBJECT_EXPLICIT_DECLARE(wrui_context);

auto wrui_create(wren_context*, struct wrio_context*) -> ref<wrui_context>;

struct wrui_scene
{
    wrui_tree*      tree;
    wrui_transform* transform;
};

auto wrui_get_scene(wrui_context*) -> wrui_scene;

// -----------------------------------------------------------------------------

enum class wrui_modifier : u32
{
    mod   = 1 << 0,
    super = 1 << 1,
    shift = 1 << 2,
    ctrl  = 1 << 3,
    alt   = 1 << 4,
    num   = 1 << 5,
    caps  = 1 << 6,
};

using wrui_scancode = u32;

struct wrui_keyboard;
struct wrui_pointer;

// -----------------------------------------------------------------------------

enum class wrui_node_type
{
    transform,
    tree,
    texture,
};

struct wrui_node : wrei_object
{
    wrui_node_type  type;      // Node type
    wrui_tree*      parent;    // Parent in the layer hierarchy, controls z-order and visibility
    wrui_transform* transform; // Parent in the transform hierarhcy, controls xy positioning
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

    std::vector<ref<wrui_node>> children;

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
    ref<wren_image> image;
    vec4u8          tint;
    aabb2f32        src;
    rect2f32        dst;
};

auto wrui_texture_create(wrui_context*) -> ref<wrui_texture>;
void wrui_texture_set_image(wrui_texture*, wren_image*);
void wrui_texture_set_tint( wrui_texture*, vec4u8   tint);
void wrui_texture_set_src(  wrui_texture*, aabb2f32 src);
void wrui_texture_set_dst(  wrui_texture*, aabb2f32 dst);
void wrui_texture_damage(   wrui_texture*, aabb2f32 damage);

// -----------------------------------------------------------------------------

// Represents a normal interactable "toplevel" window.
struct wrui_window;
WREI_OBJECT_EXPLICIT_DECLARE(wrui_window);

enum class wrui_event_type
{
    resize,
};

struct wrui_event
{
    wrui_event_type type;
    wrui_window* window;

    union {
        vec2u32 resize;
    };
};

using wrui_event_handle_fn = void(wrui_event*);

auto wrui_window_create(wrui_context*) -> ref<wrui_window>;
void wrui_window_set_event_handler(wrui_window*, std::move_only_function<wrui_event_handle_fn>&&);
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
