# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor.

## Goals

The following are guiding principals and goals in the development of Roc. **Not** a list of currently supported features.

- Independence from Wayland frameworks / libraries
   - With an exception for libwayland implementing the Wayland wire protocol for now.
- Modern Vulkan based Wayland compositor
   - Consolidating on a single backend API and modern kernel version simplifies GPU allocation, buffer sharing and synchronization code paths
- Move common desktop functionality into the compositor process via a plugin/applet system
   - Removes overhead of cross-process buffer sharing and synchronization
   - E.g.
       - Desktop background (with support for internally compressed formats)
       - System tray
       - Notification daemon
       - Output management (with support for screen size/refresh spoofing)
       - Screenshare / screenshot / screenrecord / color picking
       - Clipboard management
- Universal input device remapping
   - Keyboard, mouse, touchpad, drawing tablet, foot pedal, gamepad, joystick, etc..
   - With support for routing global shortcut combinations to particular applications
   - Compositor-aware input mapping (e.g. per-window rules)

## Non-Goals

 - Support a wide range of (especially older) hardware
 - Support every Wayland protocol
    - A lot of Wayland protocol functionality can be replaced with compositor plugins that provide more integration and simpler communication.

# Support

Roc is currently tested on the following:

- Arch Linux + AMD Mesa (RADV)

**NOTE:** Roc will *not* work on NVidia (or likely Intel) GPUs currently as it relies on Mesa's ability to import/export Vulkan timeline semaphores as DRM syncobjs via OPAQUE_FD handles.

# Architecture

The project is currently in a state of rewriting the old monolothic `wroc` compositor implementation into a more more modular layered approach.

Points marked as **❗** have not been implemented in the rewrite yet.

#### Core Utillities - `core`

- Common data structures, algorithms and systems
- Event loop (timed dispatch, file descriptor events)
- Object registry (intrusive allocation reference counting)

#### GPU Control - `gpu`

- Vulkan object wrappers
- DMABUF / DRM syncobj interop

#### Session I/O - `io`

- `process`  (child process management) **❗**
- `udev`     (device discovery, hotplug events) **❗**
- `session`  (VT switching, restricted device access) **❗**
- `libinput` (raw keyboard / mouse input) **❗**
- `evdev`    (raw gamepad / joystick input) **❗**
- `drm`      (atomic KMS display control) **❗**
- `wayland`  (nested emulation of `libinput`, `drm`)

#### Scene Compositor - `scene`

- Semi-opinionated "display server"
- Scene graph
   - Damage tracking **❗**
- Output layout
- Internal windowing API
   - Virtual clients
- Per-client input routing
   - Implicit pointer focus based on input region nodes
   - Explicit keyboard/pointer focus based on client grabs

#### Immediate Mode UI - `imui`

- ImGui multi-viewport backend
- First-class window layout integration

#### Wayland Protocols - `way`

- Adapts Wayland client state to `scene` elements
   - `wl_client` ⇾ `client`
   - `xdg_toplevel` ⇾ `window` + `texture` + `input_region`
   - `xdg_popup`, `wl_subsurface` ⇾ `texture` + `input_region` **❗**

#### Window Manager - `wm`

- Mouse driver
   - Acceleration
   - Constraints **❗**
- High-level window layout policy
   - Drag move/size
   - Grid based window placement **❗**
- Native shell components (launcher, system tray, notifications, etc..) **❗**
- Common keybindings (media keys, application launch, etc..) **❗**

# Building

#### System dependencies (build-time)

- python 3
- cmake
- ninja
- wayland-protocols
- xkbcommon
- mold
- clang (C++26 capable version at minimum)

#### Quickstart

Build in release mode and install to `.local/bin/wroc`

```
$ python build.py -BIR
```

**NOTE:** Currently this will also install a git version of `xwayland-satellite` to `.local/bin`

#### Global Queue Priority

Roc can take advantage of higher queue scheduling priority when given the NICE system capability.

```
# setcap cap_sys_nice+ep ~/.local/bin/wroc
```

#### Build Options

- `-B` : Build project
- `-I` : Install project
- `-R` : Build in release mode
- `-C` : Force reconfigure and clean build
- `-U` : Check and update dependencies (updates `build.json`)
- `--asan` : Enable address sanitizer
- `--system-slangc` : Look for `slangc` on path

Build artifacts are placed into `.build/[debug|release](-asan)` depending on build parameters.
