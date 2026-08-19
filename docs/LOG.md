# Build log

A running diary of what got built, in what order, and why — the raw
material for the full retrospective walkthrough once the project is done.

## Milestone 1 — Window + OpenGL context boot

**What:** `src/main.cpp` opens a GLFW window, requests an OpenGL 3.3 core
profile context, loads the OpenGL function pointers with GLEW, and clears
the screen every frame.

**Why this order:** nothing else can be tested without this. A camera, a
renderer, a parser — none of it means anything until there's a window that
can show a frame and a build system that reliably produces a runnable
binary. Getting the whole toolchain (CMake → MSVC → GLFW/GLEW/GLM/ImGui
downloaded and linked → an .exe that runs) working end to end first means
every later milestone is *adding* to something proven, not debugging the
toolchain and the feature at the same time.

**Verified:** built and ran; printed `NVIDIA GeForce RTX 4080` / OpenGL 3.3.0
from the real driver, window stayed open.

## Milestone 2 — Orbit camera

**What:** `include/render/Camera.h` + `src/render/Camera.cpp` — an orbit
camera (always looks at a target point, moves on a sphere around it) with
mouse-drag orbit, mouse-drag pan, scroll zoom, and four view presets
(Top/Front/Right/Iso). `include/render/GridRenderer.h` +
`src/render/GridRenderer.cpp` draw a reference grid + XYZ axis gizmo on the
XY plane so there's something in the scene to actually see the camera move
around — this is throwaway scaffolding, replaced once milestone 6 (the real
toolpath renderer) exists.

**Why an orbit camera, not a first-person camera:** every CAD/CAM/DCC tool
(Blender, Fusion 360, the original app) uses orbit cameras because the task
is "inspect this object from different angles," not "walk around a space."

**The bug this design avoids:** the naive way to build an orbit camera is
two Euler angles (yaw, pitch) fed into `glm::lookAt(eye, target, fixedUp)`
with a fixed world up-vector like `(0,0,1)`. That breaks — literally
produces NaN — the moment the camera looks straight down or straight up,
because `lookAt` can't build a camera basis when its "forward" and "up" are
parallel. Top-down is usually the *first* view anyone wants for inspecting
a toolpath on a bed, so this isn't an edge case, it's a near-certain crash.

The fix: represent the camera's full orientation as one quaternion
(`qYaw * qPitch`), and derive *both* the forward vector and the up vector
from that same quaternion, instead of computing forward from yaw/pitch and
grabbing up from nowhere. Since rotating two vectors that started
perpendicular (forward `(0,-1,0)`, up `(0,0,1)`) by the same rotation keeps
them perpendicular, `lookAt` never receives a degenerate forward/up pair —
not even at the poles. Pitch is still clamped to ±89.5° for UX reasons
(orbiting through the exact pole feels disorienting), but the math itself
would not break even without the clamp.

**Why orthographic, not perspective projection:** the original app has no
real perspective either (its `project()` function is rotation + a flat
scale factor). For inspecting toolpaths, two paths that are actually
parallel in the real world should *look* parallel on screen — perspective's
vanishing-point foreshortening actively works against that. `glm::ortho()`
with a zoom-controlled half-extent reproduces the original's framing
behavior with real matrix math instead of hand-rolled trig.

**Verified:** rather than eyeballing a window (there's no automated way to
screenshot a native GL window from this environment), the camera prints
`eye`/`forward` for all four presets at startup. Results matched the
derivation exactly: Top → forward ≈ (0, 0, -1) straight down; Front →
forward = (0,-1,0); Right → forward = (1,0,0); Iso → forward ≈ normalized
(1,-1,-1), the standard isometric direction. Build succeeded, executable ran
without hitting the shader compile/link error paths.

## Milestone 3 — Scene data model

**What:** `include/model/` — `Path`, `Layer`, `Transform`, `SelectionGroup`,
`SceneObject`, `Scene`. Pure data, zero dependency on GL/GLFW/ImGui.

**Key decision:** `SceneObject::selectedPaths` is the *only* selection
mechanism, matching what reading the original's code turned up earlier
(layer-table clicks, "select visible," and selection groups all populate
this one `std::set<int>` rather than being four separate systems).

## Milestone 4 & 5 — G-code and KUKA SRC parsers

**What happened before writing any code:** grepped the original for any
G-code (`.gcode`, G0/G1/G2/G3) handling and found none — despite "G-code" in
the product name and repo description, every parsing function that exists
is KUKA SRC-specific (`motionRe`, `coordRe`, `$VEL.CP`, `LIN`/`PTP`/`CIRC`/
`SPL`). So the milestone order flipped in practice: `SrcParser` is a direct,
regex-for-regex port of the original's `parseObject()` — same travel-marker
handling (`;TRAVEL START`/`;TRAVEL END` comments), same layer-detection rule
(new layer when a print move's Z differs from the previous print move's Z by
more than `1e-5`), same "path.from = previous path's `to`, or itself if it's
the very first path" rule. `GcodeParser` is NOT a port of anything — it's a
standard G0/G1/G2/G3 implementation with documented assumptions (G0=travel,
G1/G2/G3=print, no E-axis tracking, I/J arcs only) since there's no original
behavior to match.

**Testing:** added `gcode_core`, a static library containing only
`model/`, `parser/`, and `io/` — no OpenGL dependency at all — specifically
so the parsers could be unit-tested without a graphics context. Wrote
`tests/parser_smoke_test.cpp`: hand-traced a realistic SRC snippet
(travel-wrapped approach move, then a 5-path print across 2 layers) line by
line on paper first, encoded the expected path count/types/layers/speeds as
assertions, then ran the actual parser against it. All 19 checks passed on
the first run — meaningful because it means the hand-derivation of "what
the original's algorithm should produce" and the C++ port's actual behavior
agree, not just that the code compiles.

## Milestone 6 — Line renderer + color modes

**What:** `render/PathColorizer` reproduces the original's `pathColor()`
exactly: same 18-color hex palette, same per-mode logic (object color /
travel-orange-print-green / layer-indexed-into-palette / selection-group
color-or-fallback / speed-bucket-indexed-into-palette). `render/SceneRenderer`
walks every visible object's paths, applies `applyTransform()` to get
world-space coordinates, colors each segment, and uploads the whole thing
as one `GL_LINES` vertex buffer.

**Refactor along the way:** pulled the shader compile/link code out of
`GridRenderer` into `render/LineShader` (a `createLineShaderProgram()`
free function + a shared `LineVertex` struct) once it became clear
`SceneRenderer` needed the exact same position+color shader -- two
renderers wanting the same shader source is the point where "just copy
it" stops being the simpler option.

**Verified end-to-end:** wrote `assets/samples/sample_chair.src`, a
hand-authored 4-layer square toolpath (21 motion lines total: 1 PTP + 1
travel LIN, then 4x [1 travel LIN + 4 print LIN]). Loaded it through the
real pipeline -- `readLinesFromFile` -> `parseSrc` -> `Scene::addObject` ->
`SceneRenderer::rebuild` -- and the console output matched the hand-count
exactly: "21 paths, 4 layers" and "21 line segments" uploaded to the GPU.
This is the first milestone where the app renders something that came from
an actual file, not placeholder geometry.

## Milestone 7 — ImGui editor UI

**What:** integrated Dear ImGui (GLFW + OpenGL3 backends) and built the
control panel: an object list (select active, toggle visibility, reorder,
link to next object), a transform panel bound directly to the active
object's `Transform` (X/Y/Z/rotZ/flip, plus ±50mm nudge buttons), a layer
table (click a row to select that layer's print paths), a selection-group
manager (create from current selection, select, delete), a speed panel
(exact or reduce/increase-by-percent, applied to whatever is selected), a
color-mode picker, and File > Open wired to a native Windows file dialog
(`ui/FileDialog.cpp`, `GetOpenFileNameW`) that loads through the same
`parseSrc`/`parseGcode` pipeline milestone 6 proved out.

**Structural decision:** `editor/Selection.h` and `editor/SpeedEditing.h`
hold the actual mutation logic (selection compose, speed application) as
plain functions taking a `SceneObject&` -- `ui/EditorUI` calls them but
contains no editing logic itself. This is the same reasoning as the
gcode_core split in milestone 4/5: it means the logic that matters (does
"shift-click add to layer 1's selection" actually work?) can be
unit-tested without ImGui or a window in the loop. Added 8 more checks to
`tests/parser_smoke_test.cpp` covering replace/add/subtract selection
composition and exact/reduce/increase speed application (including that
reduce/increase compounds on the CURRENT effective speed, not the
originally parsed one, and that PTP paths are correctly skipped) -- all 27
checks across every milestone still pass.

**Selection compose without viewport picking:** the original lets you
click individual paths directly in the 3D viewport, with Shift/Ctrl for
add/subtract. That requires ray-vs-line-segment hit testing against
screen-space projected paths, which is a real feature on its own -- not
implemented yet. What IS implemented: the same Replace/Add/Subtract
semantics (read from Shift/Ctrl each frame) applied to layer-table clicks
and selection-group "Select" buttons, which covers the "select by layer,"
"select by group," and "select all visible" cases from the original
requirements. Direct in-viewport path picking is an honest gap, not a
silent omission -- worth revisiting once there's more on screen worth
clicking individually.

**Verified:** clean build, app launches, loads the sample file through the
full UI-driven path (not just the hardcoded startup load from milestone 6),
and the parser/editor logic test suite still passes after the refactor.
