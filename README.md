# Gcode Editor (C++ / OpenGL port)

A ground-up C++ rewrite of [amineroula/-Gcode-Editor](https://github.com/amineroula/-Gcode-Editor)
(an iPad-friendly browser PWA for editing G-code and KUKA SRC files for robotic
additive manufacturing), targeting a real OpenGL 3D viewport instead of a
2D-canvas pseudo-3D projection.

This repo is also a teaching project: each milestone is built and explained
step by step. See [docs/PLAN.md](docs/PLAN.md) for the roadmap and
[docs/LOG.md](docs/LOG.md) for a running diary of what we built and why.

## Stack

- **C++17**, CMake (FetchContent — no manual dependency install)
- **GLFW** — window + input
- **GLEW** — OpenGL function loading
- **GLM** — vector/matrix math
- **Dear ImGui** — editor UI (panels, file dialogs, controls)

## Build

```bash
cmake -S . -B build
cmake --build build --config Debug
```

Run `build/Debug/gcode_editor.exe` (or `build/gcode_editor` depending on generator).

## Status

Milestones 1-7 of 12 complete: window/GL boot, orbit camera, scene data
model, G-code + KUKA SRC parsers, a real colored line renderer, and an
ImGui editor (object list, transform, layer table, selection groups, speed
editing, color mode picker, File > Open for real files). See
[docs/LOG.md](docs/LOG.md) for the how/why of each step and
[docs/PLAN.md](docs/PLAN.md) for what's left (8-12).
