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

enum class wrui_node_type
{
    transform,
    tree,
    texture,
    output,
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

auto wrui_window_create(wrui_context*) -> ref<wrui_window>;
void wrui_window_set_event_handler(wrui_window*, std::move_only_function<void(wrui_event*)> event_handler);
// Sets the window frame size for decorations and layout placement.
void wrui_window_set_size(wrui_window*, vec2u32);
// Get the window tree, this is used to attach window contents to
auto wrui_window_get_tree(wrui_window*) -> wrui_tree*;
// Returns the subtree in which decorations are placed.
// If not null, this entry is a child of `get_tree`.
// This can be pass as `reference` to `place_[below/above]` when
// ordering subsurfaces.
auto wrui_window_get_decorations(wrui_window*) -> wrui_tree*;
// Get the window transform, this is used to anchor window contents to
auto wrui_window_get_transform(wrui_window*) -> wrui_transform*;
