#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct SeatCursorManager;

auto scene_cursor_manager_create(const char* theme, i32 size) -> Ref<SeatCursorManager>;

enum class SceneDamageType : u32
{
    visual = 1 << 0,
    input  = 1 << 1,
};

struct Scene
{
    ExecContext* exec;
    Gpu* gpu;

    struct {
        Ref<GpuShader> vertex;
        Ref<GpuShader> fragment;
        Ref<GpuImage> white;
        Ref<GpuSampler> sampler;
    } render;

    Ref<SceneTree> root_tree;
    EnumMap<SceneLayer, Ref<SceneTree>> layers;

    std::vector<SceneDamageListener> damage_listeners;

    RefVector<Seat> seats;

    struct {
        Flags<SceneDamageType> queued;
    } damage;

    Ref<SeatCursorManager> cursor_manager;

    ~Scene();
};

void scene_render_init(Scene*);

// -----------------------------------------------------------------------------

struct Seat
{
    Scene* scene;

    Ref<SeatKeyboard> keyboard;
    Ref<SeatPointer> pointer;
    std::vector<IoInputDevice*> led_devices;

    Ref<SeatDataSource> selection;

    std::vector<SeatEventFilter*> input_event_filters;
};

void seat_init(Scene*);

// -----------------------------------------------------------------------------

struct SeatClient
{
    Scene* scene;

    std::move_only_function<SeatEventHandlerFn> event_handler;

    u32 input_regions = 0;

    ~SeatClient();
};

void seat_client_post_event(SeatClient*, SeatEvent*);

// -----------------------------------------------------------------------------

struct SeatInputDevice
{
    Seat* seat;

    SeatInputRegion* focus;
};

// -----------------------------------------------------------------------------

struct SeatKeyboard : SeatInputDevice, SeatKeyboardInfo
{
    CountingSet<u32> pressed;

    Flags<SeatModifier> depressed;
    Flags<SeatModifier> latched;
    Flags<SeatModifier> locked;

    EnumMap<SeatModifier, xkb_mod_mask_t> mod_masks;

    ~SeatKeyboard();
};

auto seat_keyboard_create(Seat*) -> Ref<SeatKeyboard>;

// -----------------------------------------------------------------------------

struct SeatPointer : SeatInputDevice
{
    CountingSet<u32> pressed;

    Ref<SceneTree> tree;

    std::move_only_function<SeatPointerAccelFn> accel;
};

auto seat_pointer_create(Seat*) -> Ref<SeatPointer>;

void scene_update_pointers(Scene*);

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(SceneTree*, vec2f32 pos) -> SeatInputRegion*;

inline
auto scene_get_focus_client(SeatInputRegion* focus)
{
    return focus ? focus->client : nullptr;
}

// -----------------------------------------------------------------------------

struct SeatDataSource
{
    SeatClient* client;

    std::flat_set<std::string> offered;

    SeatDataSourceOps ops;

    ~SeatDataSource();
};

void scene_offer_selection(SeatClient*, SeatDataSource*);

// -----------------------------------------------------------------------------

void scene_enqueue_damage(Scene*, SceneDamageType);

// -----------------------------------------------------------------------------

struct SeatEventFilter
{
    Weak<Seat> seat;

    std::move_only_function<SeatEventFilterResult(SeatEvent*)> filter;

    ~SeatEventFilter();
};
