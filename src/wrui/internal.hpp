#pragma once

#include "wrui.hpp"

#include "wrio/wrio.hpp"

// -----------------------------------------------------------------------------

struct wrui_context
{
    wren_context* wren;

    ref<wren_pipeline> pipeline;
    ref<wren_image> white;
    ref<wren_sampler> sampler;

    ref<wrui_transform> root_transform;
    ref<wrui_tree> scene;

    std::vector<wrui_window*> windows;
};

void wrui_render_init(wrui_context*);
void wrui_render(wrui_context*, wrio_output*, wren_image*);

struct wrui_window
{
    wrui_context* ctx;

    std::move_only_function<wrui_event_handle_fn> event_handler;

    vec2u32 size;
    bool mapped;

    ref<wrui_tree> tree;
    ref<wrui_transform> transform;

    ~wrui_window();
};
