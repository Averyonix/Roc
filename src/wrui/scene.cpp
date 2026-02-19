#include "internal.hpp"

static
void request_frame(wrui_context* ctx)
{
    for (auto* output : wrio_list_outputs(ctx->wrio)) {
        wrio_output_request_frame(output, wren_image_usage::render);
    }
}

static
void damage_node(wrui_node* node)
{
    switch (node->type) {
        break;case wrui_node_type::transform:
            for (auto& child : static_cast<wrui_transform*>(node)->children) {
                damage_node(child.get());
            }
        break;case wrui_node_type::tree:
            for (auto& child : static_cast<wrui_tree*>(node)->children) {
                damage_node(child.get());
            }
        break;case wrui_node_type::texture:
            if (node->parent) {
                request_frame(node->parent->ctx);
                // TODO: Damage affected regions only.
                //       Since currently we're just forcing a global redraw,
                //       we can immediately return after a frame has been requested.
                return;
            }
    }
}

// -----------------------------------------------------------------------------

wrui_transform::~wrui_transform()
{
    for (auto& child : children) {
        child->transform = nullptr;
    }
}

auto wrui_transform_create(wrui_context*) -> ref<wrui_transform>
{
    auto transform = wrei_create<wrui_transform>();
    transform->type = wrui_node_type::transform;
    transform->local.scale  = 1;
    transform->global.scale = 1;
    return transform;
}

static
void update_transform_global(wrui_transform* transform, const wrui_transform_state& parent)
{
    transform->global.translation = parent.translation + transform->local.translation * parent.scale;
    transform->global.scale       = parent.scale * transform->local.scale;
    for (auto& child : transform->children) {
        if (child->type == wrui_node_type::transform) {
            update_transform_global(static_cast<wrui_transform*>(child.get()), transform->global);
        }
    }
}

void wrui_transform_update(wrui_transform* transform, vec2f32 translation, f32 scale)
{
    damage_node(transform);
    transform->local.translation = translation;
    transform->local.scale = scale;
    auto* parent = transform->transform;
    update_transform_global(transform, parent ? parent->global : wrui_transform_state{});
    damage_node(transform);
}

auto wrui_transform_get_local(wrui_transform* transform) -> wrui_transform_state
{
    return transform->local;
}

auto wrui_transform_get_global(wrui_transform* transform) -> wrui_transform_state
{
    return transform->global;
}

void wrui_node_set_transform(wrui_node* node, wrui_transform* transform)
{
    if (node->transform == transform) return;
    if (transform) {
        transform->children.emplace_back(node);
    }
    if (auto old_transform = std::exchange(node->transform, transform)) {
        damage_node(node);
        std::erase_if(old_transform->children, wrei_object_equals<wrui_node>{node});
    }
    if (transform) {
        damage_node(node);
    }
}

// -----------------------------------------------------------------------------

wrui_tree::~wrui_tree()
{
    for (auto& child : children) {
        child->parent = nullptr;
    }
}

auto wrui_tree_create(wrui_context* ctx) -> ref<wrui_tree>
{
    auto tree = wrei_create<wrui_tree>();
    tree->type = wrui_node_type::tree;
    tree->ctx = ctx;
    return tree;
}

void wrui_node_unparent(wrui_node* node)
{
    auto parent = std::exchange(node->parent, nullptr);
    std::erase_if(parent->children, wrei_object_equals<wrui_node>{node});
}

static
void reparent_unsafe(wrui_node* node, wrui_tree* tree)
{
    if (node->parent == tree) return;
    if (node->parent) {
        wrui_node_unparent(node);
    }
    node->parent = tree;
    // TODO: We only need to damage regions that were visually affected by the rotate
    damage_node(node);
}

static
void tree_place(wrui_tree* tree, wrui_node* reference, wrui_node* node, bool above)
{
    auto cur = std::ranges::find_if(tree->children, wrei_object_equals{node});
    auto sib = std::ranges::find_if(tree->children, wrei_object_equals{reference});

    if (sib == tree->children.end()) {
        if (reference) {
            log_error("wrui::tree_place called with reference that doesn't exist in tree, inserting at end");
        }
        if (above) tree->children.emplace_back(node);
        else       tree->children.insert(tree->children.begin(), node);
        return;
    }

    if (cur == sib) {
        log_error("wrui::tree_place called with node == reference");
        return;
    }

    if (cur > sib) std::rotate(sib + i32(above), cur, cur + 1);
    else           std::rotate(cur, cur + 1, sib + i32(above));
}

void wrui_tree_place_below(wrui_tree* tree, wrui_node* reference, wrui_node* to_place)
{
    tree_place(tree, reference, to_place, false);
    reparent_unsafe(to_place, tree);

}

void wrui_tree_place_above(wrui_tree* tree, wrui_node* reference, wrui_node* to_place)
{
    tree_place(tree, reference, to_place, true);
    reparent_unsafe(to_place, tree);
}

// -----------------------------------------------------------------------------

auto wrui_texture_create(wrui_context*) -> ref<wrui_texture>
{
    auto texture = wrei_create<wrui_texture>();
    texture->type = wrui_node_type::texture;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, wrei_minmax};
    return texture;
}

void wrui_texture_set_image(wrui_texture* texture, wren_image* image)
{
    texture->image = image;
    damage_node(texture);
}

void wrui_texture_set_tint(wrui_texture* texture, vec4u8 tint)
{
    texture->tint = tint;
    damage_node(texture);
}

void wrui_texture_set_src(wrui_texture* texture, aabb2f32 source)
{
    texture->src = source;
    damage_node(texture);
}

void wrui_texture_set_dst(wrui_texture* texture, aabb2f32 extent)
{
    damage_node(texture);
    texture->dst = extent;
    damage_node(texture);
}

void wrui_texture_damage(wrui_texture* texture, aabb2f32 damage)
{
    wrei_assert_fail("wrui_texture_damage", "TODO");
}
