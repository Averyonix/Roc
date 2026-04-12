#include "internal.hpp"

Scene::~Scene()
{
}

auto scene_create(ExecContext* exec, Gpu* gpu) -> Ref<Scene>
{
    auto scene = ref_create<Scene>();

    scene->exec = exec;
    scene->gpu = gpu;

    scene->root_tree = scene_tree_create();
    scene->root_tree->scene = scene.get();

    for (auto layer : magic_enum::enum_values<SceneLayer>()) {
        auto* tree = (scene->layers[layer] = scene_tree_create()).get();
        scene_tree_place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene_render_init(scene.get());

    scene->cursor_manager = scene_cursor_manager_create("breeze_cursors", 24);

    seat_init(scene.get());

    return scene;
}

auto scene_get_layer(Scene* scene, SceneLayer layer) -> SceneTree*
{
    return scene->layers[layer].get();
}

// -----------------------------------------------------------------------------

auto seat_add_input_event_filter(Seat* seat, std::move_only_function<SeatEventFilterResult(SeatEvent*)> fn) -> Ref<SeatEventFilter>
{
    auto filter = ref_create<SeatEventFilter>();
    filter->seat = seat;
    filter->filter = std::move(fn);
    seat->input_event_filters.emplace_back(filter.get());
    return filter;
}

SeatEventFilter::~SeatEventFilter()
{
    if (seat) {
        std::erase(seat->input_event_filters, this);
    }
}
