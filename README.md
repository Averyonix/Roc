# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor.

# Building

#### System dependencies (build-time)

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
