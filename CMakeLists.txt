cmake_minimum_required(VERSION 4.0)

project(wroc)

# ------------------------------------------------------------------------------

function(target_generated_source target visibility generated_source_name)
    string(JOIN "\n" generated_content ${ARGN})
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}"
        CONTENT "${generated_content}")
    target_sources(${target} ${visibility} "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}")
endfunction()

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               libevdev INTERFACE)
target_link_libraries(     libevdev INTERFACE evdev)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)

# ------------------------------------------------------------------------------

# TODO: Helper function for pkg config dependencies
add_library(               libgio2 INTERFACE)
target_include_directories(libgio2 INTERFACE /usr/include/glib-2.0
                                             /usr/include/gio-unix-2.0
                                             /usr/lib/glib-2.0/include)
target_link_libraries(     libgio2 INTERFACE /usr/lib/libgio-2.0.so
                                             /usr/lib/libglib-2.0.so
                                             /usr/lib/libgobject-2.0.so)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/magic-enum EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(stb-image)
target_include_directories(stb-image PUBLIC ${VENDOR_DIR}/stb)
target_generated_source(stb-image PRIVATE stb_image.c       "#define STB_IMAGE_IMPLEMENTATION 1"       "#include <stb_image.h>")
target_generated_source(stb-image PRIVATE stb_image_write.c "#define STB_IMAGE_WRITE_IMPLEMENTATION 1" "#include <stb_image_write.h>")

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/glm EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(imgui)
target_include_directories(imgui PUBLIC ${VENDOR_DIR}/imgui)
target_sources(imgui PRIVATE
    ${VENDOR_DIR}/imgui/imgui.cpp
    ${VENDOR_DIR}/imgui/imgui_demo.cpp
    ${VENDOR_DIR}/imgui/imgui_draw.cpp
    ${VENDOR_DIR}/imgui/imgui_tables.cpp
    ${VENDOR_DIR}/imgui/imgui_widgets.cpp
    ${VENDOR_DIR}/imgui/misc/cpp/imgui_stdlib.cpp
    )

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/concurrent-queue EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-headers           EXCLUDE_FROM_ALL)
add_subdirectory(${VENDOR_DIR}/vulkan-utility-libraries EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-memory-allocator  EXCLUDE_FROM_ALL)

add_library(vma-object)
target_generated_source(   vma-object PRIVATE vk_mem_alloc.cpp "#define VMA_IMPLEMENTATION" "#include <vk_mem_alloc.h>")
target_compile_options(    vma-object PRIVATE -w)
target_link_libraries(     vma-object PUBLIC GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(vma-object PUBLIC "VMA_STATIC_VULKAN_FUNCTIONS=0" "VMA_DYNAMIC_VULKAN_FUNCTIONS=1")

# ------------------------------------------------------------------------------

function(init_target target_name)
    target_compile_options(${target_name} PRIVATE
        -std=c++26
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-sometimes-uninitialized
        -Wno-missing-designated-field-initializers
        -Wno-missing-field-initializers
        -Wno-unknown-pragmas
        -Wno-gnu-conditional-omitted-operand
        -Wno-zero-length-array
        -Wno-unused-parameter
        -Wno-nested-anon-types
        -Wimplicit-fallthrough
        -Wno-gnu-case-range

        # -Wno-unused-const-variable
        # -Wno-unused-function

        -Wshadow
        )
    if(USE_ASAN)
        target_compile_options(${target_name} PUBLIC -fsanitize=address)
        target_link_libraries(${target_name} PUBLIC asan)
    endif()
endfunction()

# ------------------------------------------------------------------------------

add_library(wrei)
init_target(wrei)
target_precompile_headers(wrei PUBLIC src/wrei/pch.hpp)
target_compile_definitions(wrei PUBLIC PROGRAM_NAME="wroc" PROJECT_NAME="Roc")
target_sources(wrei PUBLIC
    src/wrei/log.cpp
    src/wrei/shm.cpp
    src/wrei/format.cpp
    src/wrei/object.cpp
    src/wrei/event.cpp
    src/wrei/stacktrace.cpp
    )
target_include_directories(wrei PUBLIC
    src
    /usr/include/drm
    )
target_link_libraries(wrei PUBLIC
    wayland-server
    wayland-client
    wayland-header
    xkbcommon
    libevdev
    input
    stdc++exp
    magic_enum::magic_enum
    Vulkan::Headers
    Vulkan::UtilityHeaders
    vma-object
    stb-image
    glm::glm
    unordered_dense::unordered_dense
    concurrentqueue
    Xcursor
    drm
    udev
    seat
    imgui
    cap
    libgio2
    gbm
    )

# ------------------------------------------------------------------------------

add_subdirectory(.build/shaders)

add_library(wren)
init_target(wren)
target_sources(wren PUBLIC
    src/wren/wren.cpp
    src/wren/functions.cpp
    src/wren/buffer.cpp
    src/wren/image.cpp
    src/wren/descriptors.cpp
    src/wren/formats.cpp
    src/wren/pipeline.cpp
    src/wren/commands.cpp
    src/wren/semaphore.cpp
    )
target_link_libraries(wren PUBLIC wrei shaders)

# ------------------------------------------------------------------------------

add_executable(wroc)
init_target(wroc)
target_sources(wroc PUBLIC
    src/wroc/client.cpp
    src/wroc/server.cpp
    src/wroc/event.cpp
    src/wroc/output.cpp
    src/wroc/keyboard.cpp
    src/wroc/pointer.cpp
    src/wroc/renderer.cpp
    src/wroc/buffer.cpp
    src/wroc/dmabuf.cpp
    src/wroc/syncobj.cpp
    src/wroc/seat.cpp
    src/wroc/shm.cpp
    src/wroc/surface.cpp
    src/wroc/subsurface.cpp
    src/wroc/interaction.cpp
    src/wroc/gestures.cpp
    src/wroc/xdg_shell.cpp
    src/wroc/viewporter.cpp
    src/wroc/data.cpp
    src/wroc/cursor.cpp
    src/wroc/imgui.cpp
    src/wroc/scene.cpp
    src/wroc/debug.cpp
    src/wroc/process.cpp
    src/wroc/decoration.cpp

    src/wroc/applets/launcher.cpp

    src/main.cpp

    src/wroc/backend/backend.cpp

    src/wroc/backend/wayland/wayland.cpp
    src/wroc/backend/wayland/seat.cpp
    src/wroc/backend/wayland/output.cpp

    src/wroc/backend/direct/backend.cpp
    src/wroc/backend/direct/output.cpp
    src/wroc/backend/direct/input.cpp
    src/wroc/backend/direct/events.cpp
    src/wroc/backend/direct/drm.cpp
    )
target_link_libraries(wroc PUBLIC wren wrei)
