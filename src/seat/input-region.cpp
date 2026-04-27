#include "internal.hpp"

#include <core/math.hpp>

SeatInputRegion::~SeatInputRegion()
{
    std::erase(client->input_regions, this);

    if (parent) {
        scene_node_unparent(this);
    }

    for (auto* seat : client->manager->seats) {
        if (auto* keyboard = seat_get_keyboard(seat)) {
            if (seat_keyboard_get_focus(keyboard) == this) {
                seat_keyboard_focus(keyboard, nullptr);
            }
        }

        if (auto* pointer = seat_get_pointer(seat)) {
            if (seat_pointer_get_focus(pointer) == this) {
                seat_pointer_focus(pointer, nullptr);
            }
        }
    }
}

void SeatInputRegion::damage(Scene* scene)
{
    scene_post_damage(scene, this);
}

auto seat_input_region_create(SeatClient* client) -> Ref<SeatInputRegion>
{
    auto region = ref_create<SeatInputRegion>();
    region->client = client;

    client->input_regions.emplace_back(region.get());

    return region;
}

void seat_input_region_set_region(SeatInputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

    input_region->region = std::move(region);

    scene_node_damage(input_region);
}

auto seat_find_input_region_at(SceneTree* tree, vec2f32 pos) -> SeatInputRegion*
{
    SeatInputRegion* region = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(tree,
        scene_iterate_default,
        [&](SceneNode* node) {
            if (auto input_region = dynamic_cast<SeatInputRegion*>(node)) {
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
