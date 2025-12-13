cmake_minimum_required(VERSION 4.0)

project(${PROJECT_NAME})

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               libevdev INTERFACE)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)

# ------------------------------------------------------------------------------

add_library(               pixman INTERFACE)
target_link_libraries(     pixman INTERFACE pixman-1)
target_include_directories(pixman INTERFACE /usr/include/pixman-1)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/backward-cpp EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/magic-enum EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(                stb-image ${VENDOR_DIR}/stb/stb_image.h)
set_source_files_properties(stb-image PUBLIC ${VENDOR_DIR}/stb/stb_image.h PROPERTIES LANGUAGE C)
target_include_directories( stb-image PUBLIC ${VENDOR_DIR}/stb)
target_compile_definitions( stb-image PRIVATE STB_IMAGE_IMPLEMENTATION=1)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/glm EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-headers           EXCLUDE_FROM_ALL)
add_subdirectory(${VENDOR_DIR}/vulkan-utility-libraries EXCLUDE_FROM_ALL)

add_subdirectory(${VENDOR_DIR}/vkwsi EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-memory-allocator  EXCLUDE_FROM_ALL)

add_library(vma-object)
file(GENERATE
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vk_mem_alloc.cpp
    CONTENT "#define VMA_IMPLEMENTATION\n#include <vk_mem_alloc.h>")
target_sources(       vma-object PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/vk_mem_alloc.cpp)
target_link_libraries(vma-object PUBLIC GPUOpen::VulkanMemoryAllocator)
target_compile_options(vma-object PRIVATE -w)
target_compile_definitions(vma-object PUBLIC "VMA_STATIC_VULKAN_FUNCTIONS=0" "VMA_DYNAMIC_VULKAN_FUNCTIONS=1")

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/sol2 EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               luajit INTERFACE)
target_link_libraries(     luajit INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${VENDOR_DIR}/luajit/src/libluajit.a)
target_include_directories(luajit INTERFACE ${VENDOR_DIR}/luajit/src)

# ------------------------------------------------------------------------------

add_executable(            ${PROJECT_NAME})
target_precompile_headers( ${PROJECT_NAME} PUBLIC src/wrei/pch.hpp)
target_compile_definitions(${PROJECT_NAME} PUBLIC "PROGRAM_NAME=\"${PROJECT_NAME}\"")
target_compile_options(    ${PROJECT_NAME} PRIVATE
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
    -Wno-c99-designator
    -Wno-unused-parameter
    -Wno-nested-anon-types
    -Wno-c23-extensions
    -Wimplicit-fallthrough
    -Wshadow
    )
if(USE_ASAN)
    target_compile_options(${PROJECT_NAME} PUBLIC -fsanitize=address)
endif()
target_include_directories(${PROJECT_NAME} PUBLIC
    src
    .build/shaders/include
    )
target_sources(            ${PROJECT_NAME} PUBLIC
    src/main.cpp

    src/wrei/log.cpp
    src/wrei/shm.cpp
    src/wrei/region.cpp

    src/wroc/client.cpp
    src/wroc/server.cpp
    src/wroc/event.cpp
    src/wroc/output.cpp
    src/wroc/keyboard.cpp
    src/wroc/pointer.cpp
    src/wroc/renderer.cpp
    src/wroc/buffer.cpp
    src/wroc/dmabuf.cpp
    src/wroc/seat.cpp
    src/wroc/shm.cpp
    src/wroc/surface.cpp
    src/wroc/subsurface.cpp
    src/wroc/interaction.cpp
    src/wroc/gestures.cpp
    src/wroc/xdg_shell.cpp
    src/wroc/data.cpp
    src/wroc/cursor.cpp

    .build/shaders/src/wroc_shaders.cpp

    src/wroc/backend/wayland/wayland.cpp
    src/wroc/backend/wayland/seat.cpp
    src/wroc/backend/wayland/output.cpp

    src/wren/wren.cpp
    src/wren/wren_init.cpp
    src/wren/wren_functions.cpp
    src/wren/wren_resources.cpp
    src/wren/wren_descriptors.cpp
    src/wren/wren_format.cpp
    )
if(USE_ASAN)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan)
endif()
target_link_libraries(${PROJECT_NAME} PUBLIC
    wayland-server
    wayland-client
    wayland-header
    xkbcommon
    pixman
    libevdev
    input
    evdev
    stdc++exp
    Backward::Object
    magic_enum::magic_enum
    sol2::sol2
    luajit
    Vulkan::Headers
    Vulkan::UtilityHeaders
    vma-object
    vk-wsi::vk-wsi
    stb-image
    unordered_dense::unordered_dense
    Xcursor
    )

add_subdirectory(test)
