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

add_subdirectory(${VENDOR_DIR}/vulkan-headers           EXCLUDE_FROM_ALL)
add_subdirectory(${VENDOR_DIR}/vulkan-utility-libraries EXCLUDE_FROM_ALL)
add_subdirectory(${VENDOR_DIR}/vulkan-memory-allocator  EXCLUDE_FROM_ALL)

add_subdirectory(${VENDOR_DIR}/vkwsi EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/sol2 EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               luajit INTERFACE)
target_link_libraries(     luajit INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${VENDOR_DIR}/luajit/src/libluajit.a)
target_include_directories(luajit INTERFACE ${VENDOR_DIR}/luajit/src)

# ------------------------------------------------------------------------------

add_executable(            ${PROJECT_NAME})
target_precompile_headers( ${PROJECT_NAME} PUBLIC src/common/pch.hpp)
target_compile_definitions(${PROJECT_NAME} PUBLIC "PROGRAM_NAME=\"${PROJECT_NAME}\"")
target_compile_options(    ${PROJECT_NAME} PUBLIC
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
    -Wno-unused-parameter # ...
    -Wimplicit-fallthrough
    -Wshadow
    )
if(USE_ASAN)
    target_compile_options(${PROJECT_NAME} PUBLIC -fsanitize=address)
endif()
target_include_directories(${PROJECT_NAME} PUBLIC
    src
    .build/generated/include
    )
target_sources(            ${PROJECT_NAME} PUBLIC
    src/main.cpp

    src/common/log.cpp
    src/common/event_loop.cpp

    src/compositor/server.cpp
    src/compositor/output.cpp
    src/compositor/keyboard.cpp
    src/compositor/pointer.cpp

    src/compositor/protocol/protocol.cpp

    src/backend/wayland/wayland.cpp
    src/backend/wayland/seat.cpp
    src/backend/wayland/output.cpp

    src/renderer/renderer.cpp
    src/renderer/vulkan_context_init.cpp
    src/renderer/vulkan_context.cpp
    src/renderer/vulkan_functions.cpp
    src/renderer/vulkan_helpers.cpp
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
    vk-wsi::vk-wsi
    stb-image
    )

add_subdirectory(test)
