#include "base.hpp"

#include <core/math.hpp>

SceneInputRegion::~SceneInputRegion()
{
    scene_node_unparent(this);
}

void SceneInputRegion::damage(Scene* scene)
{
    scene_post_damage(scene, this);
}

auto scene_input_region_create() -> Ref<SceneInputRegion>
{
    auto region = ref_create<SceneInputRegion>();

    return region;
}

void scene_input_region_set_region(SceneInputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

#if SCENE_NOISY_NODES
    NODE_LOG("scene.input_region{{{}}}.set_region([{:s}])", (void*)input_region,
        region.aabbs
            | std::views::transform([&](auto& aabb) { return std::format("{}", aabb); })
            | std::views::join_with(", "sv));
#endif

    input_region->region = std::move(region);

    scene_node_damage(input_region);
}

auto scene_find_input_region_at(SceneTree* tree, vec2f32 pos) -> SceneInputRegion*
{
    SceneInputRegion* region = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(tree,
        scene_iterate_default,
        [&](SceneNode* node) {
            if (auto input_region = dynamic_cast<SceneInputRegion*>(node)) {
                if (input_region->region.contains(pos - scene_tree_get_position(input_region->parent))) {
                    region = input_region;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        },
        scene_iterate_default);

    return region;
}
