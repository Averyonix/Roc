# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor.

## Goals

 - Independence from Wayland frameworks / libraries
    - With an exception for libwayland implementing the Wayland wire protocol
 - Modern Vulkan based Wayland compositor
    - Consolidating on a single backend API simplifies GPU allocation, sharing and synchronization code paths
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

 - Support a wide range of (older) hardware
    - Currently focused on a modern RADV driver stack for simplicity
 - Support every Wayland protocol
    - Some Wayland protocol functionality can be replaced with plugin based functionality

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

```
$ python build.py -BI                           build + install
# setcap cap_sys_nice+ep ~/.local/bin/wroc      give NICE capability for global queue priority
```
