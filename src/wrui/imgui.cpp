#include "internal.hpp"

static ImGuiKey imgui_key_from_xkb_sym(xkb_keysym_t);

void wrui_imgui_handle_key(wrui_context* ctx, xkb_keysym_t sym, bool pressed, const char* utf8)
{
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(imgui_key_from_xkb_sym(sym), pressed);
    if (pressed) io.AddInputCharactersUTF8(utf8);
    wrui_imgui_request_frame(ctx);
}

void wrui_imgui_handle_mods(wrui_context* ctx, flags<wrui_modifier> mods)
{
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Shift, mods.contains(wrui_modifier::shift));
    io.AddKeyEvent(ImGuiMod_Ctrl,  mods.contains(wrui_modifier::ctrl));
    io.AddKeyEvent(ImGuiMod_Alt,   mods.contains(wrui_modifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, mods.contains(wrui_modifier::super));
    wrui_imgui_request_frame(ctx);
}

void wrui_imgui_handle_motion(wrui_context* ctx)
{
    auto& io = ImGui::GetIO();
    auto pos = wrui_transform_get_global(ctx->pointer->transform.get()).translation - ctx->imgui.region.origin;
    io.AddMousePosEvent(pos.x, pos.y);
    wrui_imgui_request_frame(ctx);
}

void wrui_imgui_handle_button(wrui_context* ctx, wrui_scancode code, bool pressed)
{
    auto& io = ImGui::GetIO();
    switch (code) {
        break;case BTN_LEFT:   io.AddMouseButtonEvent(ImGuiMouseButton_Left,   pressed); wrui_imgui_request_frame(ctx);
        break;case BTN_RIGHT:  io.AddMouseButtonEvent(ImGuiMouseButton_Right,  pressed); wrui_imgui_request_frame(ctx);
        break;case BTN_MIDDLE: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, pressed); wrui_imgui_request_frame(ctx);
    }
}

void wrui_imgui_handle_wheel(wrui_context* ctx, vec2f32 delta)
{
    auto& io = ImGui::GetIO();
    io.AddMouseWheelEvent(delta.x, -delta.y);
    wrui_imgui_request_frame(ctx);
}

void wrui_imgui_init(wrui_context* ctx)
{
    ctx->imgui.region = {{}, {1920, 1080}, wrei_xywh};

    ctx->imgui.context = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx->imgui.context);

    auto& style = ImGui::GetStyle();
    style.FrameRounding     = 3;
    style.ScrollbarRounding = 3;
    style.WindowRounding    = 5;
    style.WindowBorderSize  = 0;

    auto& io = ImGui::GetIO();

    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        ctx->imgui.font_image = wren_image_create(ctx->wren, {width, height},
            wren_format_from_drm(DRM_FORMAT_ABGR8888),
            wren_image_usage::texture | wren_image_usage::transfer);
        wren_image_update_immed(ctx->imgui.font_image.get(), pixels);
    }
}

static
void request_frame(wrui_context* ctx)
{
    // TODO: Request frames for outputs covered by `imgui.region`
    for (auto* output : wrio_list_outputs(ctx->wrio)) {
        wrio_output_request_frame(output, ctx->render.usage);
    }
}

void wrui_imgui_request_frame(wrui_context* ctx)
{
    // As ImGui always works based on the *last* frame state. We need to double pump frames
    // in order to ensure that input has been tested against the latest state.
    ctx->imgui.frames_requested = 2;

    request_frame(ctx);
}

void wrui_imgui_request_frame(wrui_client* client)
{
    wrui_imgui_request_frame(client->ctx);
}

static
void reset_frame_textures(wrui_context* ctx)
{
    ctx->imgui.textures.clear();

    // Leave 0 as an invalid texture id
    ctx->imgui.textures.emplace_back();
}

auto wrui_imgui_get_texture(wrui_context* ctx, wren_image* image, wren_sampler* sampler, wren_blend_mode blend) -> ImTextureID
{
    auto idx = ctx->imgui.textures.size();
    ctx->imgui.textures.emplace_back(image, sampler, blend);
    return idx;
}

void wrui_imgui_frame(wrui_context* ctx)
{
    if (!ctx->imgui.frames_requested) return;
    ctx->imgui.frames_requested--;

    if (ctx->imgui.frames_requested) {
        request_frame(ctx);
    }

    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(ctx->imgui.region.extent.x, ctx->imgui.region.extent.y);

    reset_frame_textures(ctx);
    io.Fonts->SetTexID(wrui_imgui_get_texture(ctx, ctx->imgui.font_image.get(), ctx->render.sampler.get(), wren_blend_mode::postmultiplied));

    ImGui::NewFrame();

    for (auto* client : ctx->clients) {
        wrui_client_post_event(client, wrei_ptr_to(wrui_event {
            .type = wrui_event_type::imgui_frame,
        }));
    }

    ImGui::Render();

    if (ctx->imgui.draws) {
        wrui_node_unparent(ctx->imgui.draws.get());
    }

    auto data = ImGui::GetDrawData();

    auto scene = wrui_get_scene(ctx);

    ctx->imgui.draws = wrui_tree_create(ctx);

    for (auto& list : std::span(data->CmdLists.Data, data->CmdLists.Size)) {
        for (auto& cmd : std::span(list->CmdBuffer.Data, list->CmdBuffer.Size)) {
            auto mesh = wrui_mesh_create(ctx);

            auto indices = std::span(list->IdxBuffer.Data + cmd.IdxOffset, cmd.ElemCount);

            ImDrawIdx max_vtx = 0;
            for (ImDrawIdx idx : indices) {
                max_vtx = std::max(max_vtx, idx);
            }

            auto[image, sampler, blend] = ctx->imgui.textures[cmd.GetTexID()];

            wrei_assert(sizeof(wrui_vertex) ==  sizeof(ImDrawVert) && alignof(wrui_vertex) == alignof(ImDrawVert));

            wrui_mesh_update(mesh.get(), image.get(), sampler.get(), blend,
                {{cmd.ClipRect.x, cmd.ClipRect.y}, {cmd.ClipRect.z, cmd.ClipRect.w}, wrei_minmax},
                std::span(reinterpret_cast<wrui_vertex*>(list->VtxBuffer.Data) + cmd.VtxOffset, max_vtx + 1),
                indices);

            wrui_node_set_transform(mesh.get(), scene.transform);
            wrui_tree_place_above(ctx->imgui.draws.get(), nullptr, mesh.get());
        }
    }

    // TODO|FIXME: Separate the scene into several subtrees for each relevant UI layer.
    wrui_tree_place_above(scene.tree, nullptr, ctx->imgui.draws.get());
    wrui_tree_place_above(scene.tree, nullptr, ctx->pointer->visual.get());
}

// -----------------------------------------------------------------------------

static
ImGuiKey imgui_key_from_xkb_sym(xkb_keysym_t sym)
{
    switch (sym) {
        case XKB_KEY_Shift_L:   return ImGuiKey_LeftShift;
        case XKB_KEY_Shift_R:   return ImGuiKey_RightShift;
        case XKB_KEY_Control_L: return ImGuiKey_LeftCtrl;
        case XKB_KEY_Control_R: return ImGuiKey_RightCtrl;
        case XKB_KEY_Alt_L:     return ImGuiKey_LeftAlt;
        case XKB_KEY_Alt_R:     return ImGuiKey_RightAlt;
        case XKB_KEY_Super_L:   return ImGuiKey_LeftSuper;
        case XKB_KEY_Super_R:   return ImGuiKey_RightSuper;

        case XKB_KEY_Tab:       return ImGuiKey_Tab;
        case XKB_KEY_Left:      return ImGuiKey_LeftArrow;
        case XKB_KEY_Right:     return ImGuiKey_RightArrow;
        case XKB_KEY_Up:        return ImGuiKey_UpArrow;
        case XKB_KEY_Down:      return ImGuiKey_DownArrow;
        case XKB_KEY_Page_Up:   return ImGuiKey_PageUp;
        case XKB_KEY_Page_Down: return ImGuiKey_PageDown;
        case XKB_KEY_Home:      return ImGuiKey_Home;
        case XKB_KEY_End:       return ImGuiKey_End;
        case XKB_KEY_Insert:    return ImGuiKey_Insert;
        case XKB_KEY_Delete:    return ImGuiKey_Delete;
        case XKB_KEY_BackSpace: return ImGuiKey_Backspace;
        case XKB_KEY_space:     return ImGuiKey_Space;
        case XKB_KEY_Return:    return ImGuiKey_Enter;
        case XKB_KEY_Escape:    return ImGuiKey_Escape;
        case XKB_KEY_Menu:      return ImGuiKey_Menu;

        case XKB_KEY_0: return ImGuiKey_0;
        case XKB_KEY_1: return ImGuiKey_1;
        case XKB_KEY_2: return ImGuiKey_2;
        case XKB_KEY_3: return ImGuiKey_3;
        case XKB_KEY_4: return ImGuiKey_4;
        case XKB_KEY_5: return ImGuiKey_5;
        case XKB_KEY_6: return ImGuiKey_6;
        case XKB_KEY_7: return ImGuiKey_7;
        case XKB_KEY_8: return ImGuiKey_8;
        case XKB_KEY_9: return ImGuiKey_9;

        case XKB_KEY_A: case XKB_KEY_a: return ImGuiKey_A;
        case XKB_KEY_B: case XKB_KEY_b: return ImGuiKey_B;
        case XKB_KEY_C: case XKB_KEY_c: return ImGuiKey_C;
        case XKB_KEY_D: case XKB_KEY_d: return ImGuiKey_D;
        case XKB_KEY_E: case XKB_KEY_e: return ImGuiKey_E;
        case XKB_KEY_F: case XKB_KEY_f: return ImGuiKey_F;
        case XKB_KEY_G: case XKB_KEY_g: return ImGuiKey_G;
        case XKB_KEY_H: case XKB_KEY_h: return ImGuiKey_H;
        case XKB_KEY_I: case XKB_KEY_i: return ImGuiKey_I;
        case XKB_KEY_J: case XKB_KEY_j: return ImGuiKey_J;
        case XKB_KEY_K: case XKB_KEY_k: return ImGuiKey_K;
        case XKB_KEY_L: case XKB_KEY_l: return ImGuiKey_L;
        case XKB_KEY_M: case XKB_KEY_m: return ImGuiKey_M;
        case XKB_KEY_N: case XKB_KEY_n: return ImGuiKey_N;
        case XKB_KEY_O: case XKB_KEY_o: return ImGuiKey_O;
        case XKB_KEY_P: case XKB_KEY_p: return ImGuiKey_P;
        case XKB_KEY_Q: case XKB_KEY_q: return ImGuiKey_Q;
        case XKB_KEY_R: case XKB_KEY_r: return ImGuiKey_R;
        case XKB_KEY_S: case XKB_KEY_s: return ImGuiKey_S;
        case XKB_KEY_T: case XKB_KEY_t: return ImGuiKey_T;
        case XKB_KEY_U: case XKB_KEY_u: return ImGuiKey_U;
        case XKB_KEY_V: case XKB_KEY_v: return ImGuiKey_V;
        case XKB_KEY_W: case XKB_KEY_w: return ImGuiKey_W;
        case XKB_KEY_X: case XKB_KEY_x: return ImGuiKey_X;
        case XKB_KEY_Y: case XKB_KEY_y: return ImGuiKey_Y;
        case XKB_KEY_Z: case XKB_KEY_z: return ImGuiKey_Z;

        case XKB_KEY_F1:  return ImGuiKey_F1;
        case XKB_KEY_F2:  return ImGuiKey_F2;
        case XKB_KEY_F3:  return ImGuiKey_F3;
        case XKB_KEY_F4:  return ImGuiKey_F4;
        case XKB_KEY_F5:  return ImGuiKey_F5;
        case XKB_KEY_F6:  return ImGuiKey_F6;
        case XKB_KEY_F7:  return ImGuiKey_F7;
        case XKB_KEY_F8:  return ImGuiKey_F8;
        case XKB_KEY_F9:  return ImGuiKey_F9;
        case XKB_KEY_F10: return ImGuiKey_F10;
        case XKB_KEY_F11: return ImGuiKey_F11;
        case XKB_KEY_F12: return ImGuiKey_F12;
        case XKB_KEY_F13: return ImGuiKey_F13;
        case XKB_KEY_F14: return ImGuiKey_F14;
        case XKB_KEY_F15: return ImGuiKey_F15;
        case XKB_KEY_F16: return ImGuiKey_F16;
        case XKB_KEY_F17: return ImGuiKey_F17;
        case XKB_KEY_F18: return ImGuiKey_F18;
        case XKB_KEY_F19: return ImGuiKey_F19;
        case XKB_KEY_F20: return ImGuiKey_F20;
        case XKB_KEY_F21: return ImGuiKey_F21;
        case XKB_KEY_F22: return ImGuiKey_F22;
        case XKB_KEY_F23: return ImGuiKey_F23;
        case XKB_KEY_F24: return ImGuiKey_F24;

        case XKB_KEY_apostrophe:   return ImGuiKey_Apostrophe;
        case XKB_KEY_comma:        return ImGuiKey_Comma;
        case XKB_KEY_minus:        return ImGuiKey_Minus;
        case XKB_KEY_period:       return ImGuiKey_Period;
        case XKB_KEY_slash:        return ImGuiKey_Slash;
        case XKB_KEY_semicolon:    return ImGuiKey_Semicolon;
        case XKB_KEY_equal:        return ImGuiKey_Equal;
        case XKB_KEY_bracketleft:  return ImGuiKey_LeftBracket;
        case XKB_KEY_backslash:    return ImGuiKey_Backslash;
        case XKB_KEY_bracketright: return ImGuiKey_RightBracket;
        case XKB_KEY_grave:        return ImGuiKey_GraveAccent;
        case XKB_KEY_Caps_Lock:    return ImGuiKey_CapsLock;
        case XKB_KEY_Scroll_Lock:  return ImGuiKey_ScrollLock;
        case XKB_KEY_Num_Lock:     return ImGuiKey_NumLock;
        case XKB_KEY_Print:        return ImGuiKey_PrintScreen;
        case XKB_KEY_Pause:        return ImGuiKey_Pause;

        case XKB_KEY_KP_0: return ImGuiKey_Keypad0;
        case XKB_KEY_KP_1: return ImGuiKey_Keypad1;
        case XKB_KEY_KP_2: return ImGuiKey_Keypad2;
        case XKB_KEY_KP_3: return ImGuiKey_Keypad3;
        case XKB_KEY_KP_4: return ImGuiKey_Keypad4;
        case XKB_KEY_KP_5: return ImGuiKey_Keypad5;
        case XKB_KEY_KP_6: return ImGuiKey_Keypad6;
        case XKB_KEY_KP_7: return ImGuiKey_Keypad7;
        case XKB_KEY_KP_8: return ImGuiKey_Keypad8;
        case XKB_KEY_KP_9: return ImGuiKey_Keypad9;

        case XKB_KEY_KP_Decimal:  return ImGuiKey_KeypadDecimal;
        case XKB_KEY_KP_Divide:   return ImGuiKey_KeypadDivide;
        case XKB_KEY_KP_Multiply: return ImGuiKey_KeypadMultiply;
        case XKB_KEY_KP_Subtract: return ImGuiKey_KeypadSubtract;
        case XKB_KEY_KP_Add:      return ImGuiKey_KeypadAdd;
        case XKB_KEY_KP_Enter:    return ImGuiKey_KeypadEnter;
        case XKB_KEY_KP_Equal:    return ImGuiKey_KeypadEqual;

        case XKB_KEY_KP_Home:     return ImGuiKey_Home;
        case XKB_KEY_KP_End:      return ImGuiKey_End;
        case XKB_KEY_KP_Prior:    return ImGuiKey_PageUp;
        case XKB_KEY_KP_Next:     return ImGuiKey_PageDown;
        case XKB_KEY_KP_Insert:   return ImGuiKey_Insert;
        case XKB_KEY_KP_Delete:   return ImGuiKey_Delete;

        case XKB_KEY_KP_Left:     return ImGuiKey_LeftArrow;
        case XKB_KEY_KP_Right:    return ImGuiKey_RightArrow;
        case XKB_KEY_KP_Up:       return ImGuiKey_UpArrow;
        case XKB_KEY_KP_Down:     return ImGuiKey_DownArrow;

        // TODO: ImGuiKey_AppBack
        // TODO: ImGuiKey_AppForward
        // TODO: ImGuiKey_Oem102
    }

    return ImGuiKey_None;
}
