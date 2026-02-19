#pragma once

// -----------------------------------------------------------------------------

#include <thread>
#include <print>
#include <vector>
#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <typeinfo>
#include <span>
#include <string_view>
#include <optional>
#include <filesystem>
#include <variant>
#include <bit>
#include <algorithm>
#include <source_location>
#include <list>
#include <ranges>
#include <random>
#include <stacktrace>
#include <flat_set>
#include <flat_map>
#include <mutex>
#include <deque>

#include <cstring>
#include <csignal>

// -----------------------------------------------------------------------------

#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <linux/dma-buf.h>

// -----------------------------------------------------------------------------

#include <libinput.h>

// -----------------------------------------------------------------------------

extern "C" {
    #include <libseat.h>
}

// -----------------------------------------------------------------------------

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

// -----------------------------------------------------------------------------

#include <X11/Xcursor/Xcursor.h>

// -----------------------------------------------------------------------------

#include <xkbcommon/xkbcommon.h>
#include <libevdev/libevdev.h>

// -----------------------------------------------------------------------------

#define MAGIC_ENUM_ENABLE_HASH
#include <magic_enum/magic_enum.hpp>

// -----------------------------------------------------------------------------

#include <ankerl/unordered_dense.h>

// -----------------------------------------------------------------------------

#include <concurrentqueue.h>

// -----------------------------------------------------------------------------

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

// -----------------------------------------------------------------------------

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

// -----------------------------------------------------------------------------

#ifndef VK_NO_PROTOTYPES
# define VK_NO_PROTOTYPES
#endif

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/utility/vk_format_utils.h>

#include <vk_mem_alloc.h>

// -----------------------------------------------------------------------------

#include <stb_image.h>
#include <stb_image_write.h>

// -----------------------------------------------------------------------------

using namespace std::literals;
