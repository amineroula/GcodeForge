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

## Post-milestone-7 fixes — packaging path bug, Maya-style navigation

Two changes made after testing the first packaged build on the desk PC:

**Startup sample path bug:** the sample chair loaded fine from the dev
build but showed an empty scene in the distributed zip -- the path was
baked in at compile time as an absolute path on the dev machine
(`ASSETS_DIR`), which obviously doesn't exist once the app is copied
elsewhere. Added `executableDirectory()` (`io/FileIO`, using
`GetModuleFileNameW`) so the app looks for `assets/` next to its own
`.exe` first, falling back to the compile-time dev path only if that's not
found. Verified by copying the built exe+assets to an unrelated temp
directory and confirming the console printed the portable path, not the
fallback -- confirming it wasn't accidentally working only because the dev
path still happened to exist on the test machine too.

**Maya-style navigation + projection toggle:** the original camera used
plain left-drag=orbit, right-drag=pan, which felt wrong to someone used to
Maya's convention and also stomps on future viewport click-to-select
(milestone 7's documented gap) since a plain click would always move the
camera. Changed to Alt+LMB=orbit, Alt+MMB=pan, Alt+RMB (or plain
scroll)=zoom -- now a plain click in the viewport does nothing to the
camera, which is exactly the room needed for path picking later.

Also added `Camera::Projection` (Perspective/Orthographic, toggleable) --
previously the camera was ortho-only. Both projections share the same
`zoomFactor_`: it drives the ortho half-extent as before, and now also
drives `currentDistance()` (the perspective orbit radius), so scrolling
zooms consistently regardless of which projection is active, and switching
projection mid-session doesn't reset how "zoomed in" the view feels.
`EditorUI` gained a View panel: Perspective/Orthographic radio buttons and
Top/Front/Right/Iso buttons, so the view presets aren't keyboard-only
anymore. Verified: clean build, app runs, existing test suite (27 checks,
untouched by this change) still passes.

## Post-milestone-7 addition — Geometry (bead) render mode

Second view mode alongside the existing thin-line renderer: print paths
now optionally render as solid rectangular "bead" boxes approximating the
actual deposited material cross-section, while travel paths stay as thin
lines (there's nothing to show as a bead on a nozzle-up move).

**`render/GeometryRenderer`:** each print-path segment becomes an indexed
box -- 8 vertices, 36 indices (12 triangles) -- not 36 raw unindexed
vertices. This directly answers the "make it optimized, these are big
files" part of the ask: indexing means vertex memory doesn't triple for no
reason once files get into the tens/hundreds of thousands of segments that
docs/PLAN.md milestone 11 is ultimately about. Per-vertex normals are
computed from the cross-section corner only (ignoring the along-length
direction) -- this avoids a stretching artifact on long thin segments and,
as a side effect, makes the shading read as a smooth rounded bead rather
than a faceted block, which is arguably a better visual match for actual
deposited material anyway.

**`render/MeshShader`:** a second shader (position+normal+color, one fixed
directional light, ambient floor + diffuse) alongside `LineShader` --
lines don't need lighting, solid geometry does, or it reads as a flat
silhouette instead of a volume.

**Same rebuild discipline as milestone 6, extended:** `main.cpp` now owns
both `SceneRenderer` and `GeometryRenderer` but only rebuilds and draws
whichever one `RenderSettings::mode` currently points at -- switching to
Geometry mode doesn't touch the line buffer, and staying in Lines mode
never builds bead geometry nobody's looking at. This is real but limited
optimization (don't do wasted work), explicitly NOT the deeper adaptive-LOD
system (culling, screen-space simplification) milestone 11 covers -- that
remains a separate, larger piece of work.

**Verified:** since there's no way to click the UI's radio button from
this environment, added a startup sanity check that calls
`GeometryRenderer::rebuild()` directly regardless of the UI's default mode
and checks `glGetError()`. Result against the sample chair: 192 triangles,
`glGetError() == GL_NO_ERROR`. Hand-check: 16 of the sample's 21 paths are
print paths (4 layers x 4 sides), 16 x 12 triangles/box = 192 -- exact
match. Existing 27-check test suite unaffected (GeometryRenderer needs a
live GL context, so it isn't part of the headless gcode_core test target;
this startup check is the equivalent verification for GL-dependent code).

## Post-milestone-7 batch — mitered geometry, bed panel, picking, undo/redo, ImGui scroll fix

Six changes made together in response to feedback after testing a large
file: geometry mode was fast but had a gap artifact between boxes and the
user wanted it faster still; a bed panel; real path selection (not just
layer-table clicks); undo/redo; and a bug where panels could only be
scrolled with the scrollbar, not the mouse wheel.

**ImGui scroll/keyboard bug, root cause:** `ImGui_ImplGlfw_InitForOpenGL`
installs its own GLFW scroll/key callbacks. main.cpp then called
`glfwSetScrollCallback`/`glfwSetKeyCallback` again afterward for the
viewport's own controls -- GLFW only allows ONE callback per event type,
so the second call silently replaced ImGui's, and ImGui never received
scroll or key events again. Fixed by switching to
`ImGui_ImplGlfw_InitForOpenGL(window, false)` and installing every
callback (scroll, key, mouse button, cursor pos, char) ourselves, each one
forwarding to the matching `ImGui_ImplGlfw_*Callback` first, then running
our own logic gated on `WantCaptureMouse`/`WantCaptureKeyboard`. This is
the officially recommended pattern whenever an app needs its own GLFW
callbacks alongside ImGui.

**Geometry gap fix (`render/GeometryRenderer`), rewritten around
"runs":** consecutive, position-connected print paths are now merged into
one continuous mitered tube instead of each segment getting its own
disconnected box. At each interior joint, the cross-section orientation is
the miter (average) of the incoming and outgoing segment directions, so
adjacent segments share an exactly-matching cross-section -- that's the
gap fix. It's also a real performance win: a run of N segments needs
exactly 2 end caps total (one at each true end), not 2N -- on the sample
file this cut triangle count from 192 to 144 (a run of 4 connected
segments: 4x8=32 side triangles + 2x2=4 cap triangles = 36 per run x 4
layers = 144, confirmed by hand and by the startup sanity check). On a
file made of many long, straight, connected runs (the common case), the
saving scales with run length, not segment count.

**Bed panel (`render/BedSettings`, reworked `GridRenderer`):** the grid
is no longer fixed at construction time -- `GridRenderer::rebuild(const
BedSettings&)` regenerates it from width/depth/origin/grid-spacing
(default 100mm = 10cm per line, as asked). The origin axis gizmo stays
fixed at the true world origin regardless of bed position, so there's
always a stable reference frame even when the bed itself is moved. A
separate "Bed" ImGui window is anchored to the right edge of the screen
(`ImGui::GetIO().DisplaySize.x` minus a margin), distinct from the main
left-side Editor panel.

**Viewport path selection (`editor/Picking`):** screen-space picking, not
a 3D ray-vs-segment test -- each path's endpoints are projected through
the view-projection matrix once, then a 2D point-to-segment or
point-in-rectangle test runs in screen space. Deliberately brute-force
(no spatial index): picking runs once per click, not per frame, so even
100k+ segments is well under a millisecond -- this is NOT the milestone 11
LOD system and doesn't need to be. Plain click picks the nearest path
within a small pixel radius; plain drag (past a small threshold, to
distinguish it from a click) marquee-selects every path whose screen-space
MIDPOINT falls inside the dragged rectangle, drawn live via
`ImGui::GetForegroundDrawList()`. Shift/Ctrl still compose
(add/subtract) exactly as the layer-table and group selection already did.

**Performance-correctness fix applied to selection generally:** selecting
paths (viewport click/drag, layer-table row, group "Select") used to flow
through the same `dirty` flag as structural edits, which forced a full
`SceneRenderer`/`GeometryRenderer` rebuild on every selection change --
wasteful, and directly working against "make it faster." Split into two
flags: `sceneDirty` (structural changes -- transform, speed, visibility,
color/render mode) triggers a full rebuild; `selectionDirty` (pure
selection changes) only rebuilds the new lightweight
`SelectionHighlightRenderer` (a bright-yellow overlay drawn on top of
either render mode). Re-selecting paths on a huge file is now cheap
regardless of file size.

**Undo/redo (`editor/UndoStack`):** whole-scene snapshot undo, matching
the original's own `pushUndo()` design (Scene/SceneObject/Path are plain
copyable data with no owned resources, so "undo" is just "restore a saved
copy"). Two recording modes: `snapshotBeforeChange()` for discrete
single-click actions (nudge buttons, visibility toggles, apply-speed,
create/delete group, reorder, link toggle), and
`beginContinuousEdit()`/`commitContinuousEdit()` for drag/text fields
(transform X/Y/Z/rotZ) using `ImGui::IsItemActivated()`/
`IsItemDeactivatedAfterEdit()` so a slider drag produces ONE undo entry
for the whole drag, not one per frame it's held. Wired to Ctrl+Z/Ctrl+Y
(polled with edge detection, same pattern as the camera's Alt-drag) and an
Edit menu with Undo/Redo items showing the shortcut and greying out when
there's nothing to undo/redo. Selection changes are deliberately NOT
undoable, matching the original's behavior.

**Verified:** 34 checks now (was 27) -- added `testUndoStack()` (snapshot
undo/redo, and specifically that a committed continuous edit undoes to the
PRE-drag value, not an intermediate one) and `testPicking()` (nearest-path
and rectangle-select against hand-computed screen coordinates from a known
orthographic projection). All pass. Geometry mode's triangle count and
`glGetError()` re-verified at 144/GL_NO_ERROR after the run-merging
rewrite. Full app builds and launches cleanly.

## Post-milestone-7 batch 2 — bug fixes from photographed testing

Six more fixes, this time from screenshots of the app actually running on
the desk PC with a real (non-sample) file loaded via File > Open.

**Pan direction reversed.** `Camera::pan()` had `target_ -= right*dx;
target_ += up*dy;` -- flipped both signs so dragging feels like grabbing
the scene and moving it with the cursor, not fighting it.

**Selection invisible in Geometry mode -- root cause diagnosed, not just
patched.** `SelectionHighlightRenderer` draws each selected path's
CENTERLINE. In Lines mode that's the only geometry there, so it's fine. In
Geometry mode the centerline sits INSIDE the solid bead box -- with depth
testing on (which it is, for correct 3D rendering generally), the box's
own opaque front surface is nearer the camera than its own centerline, so
the depth test correctly rejects drawing the highlight there. This isn't a
rendering glitch, it's the depth test doing exactly its job against
geometry that happens to occlude itself. Real fix: `GeometryRenderer` now
bakes `selectionHighlightColor()` directly into the mesh/travel-line
vertex colors for selected paths, instead of relying on a separate overlay
that can't see through solid geometry. Also standardized the highlight
color itself to bright green (`#39ff5a`) per feedback, shared via
`render/PathColorizer::selectionHighlightColor()` so both renderers use
literally the same value.

**Marquee rectangle getting stuck on screen during camera orbit -- a real
state-machine bug.** Traced from the screenshot showing a giant yellow
rectangle covering the viewport during what should have been a plain
rotate: `isDraggingMarquee` was only ever reset to `false` on the NEXT
mouse-down, never immediately after a drag was released. Alt+LMB-drag
(orbit) also holds the left mouse button -- so the marquee draw condition
(`isDraggingMarquee && leftButtonPressed`) would go true again the moment
the user held Alt+LMB to orbit after any earlier click, and then track the
rotate drag as if it were still marquee-selecting. Fixed by (1) explicitly
resetting `isDraggingMarquee` immediately after handling a release, (2)
resetting it whenever Alt is held (Alt means camera nav, never selection),
and (3) moving `leftPressed`/`leftWasPressed` edge-detection to update
unconditionally every frame instead of only inside whichever branch
happened to run, so toggling Alt mid-drag can't leave the edge-detection
state stale either. Also added a defense-in-depth `!altHeld` check on the
marquee's actual draw call.

**Objects panel: added Delete and a per-object color swatch.** Delete
needed care -- erasing mid-loop over `scene.objects` while iterating by
index would invalidate later iterations, so the actual erase is deferred
to right after the table finishes drawing for the frame, and if the
deleted object was active, `activeObjectId` falls back to whatever's now
first (or 0 if the scene is now empty). The color swatch
(`ImGui::ColorEdit3` bound to `object.color`) uses the same
begin/commitContinuousEdit undo pattern as the transform fields, since
dragging inside a color picker is exactly the same "many frames, one
logical edit" shape as dragging a slider.

**UI looked "weak" -- swapped the default ImGui font for Segoe UI.** The
single biggest lever for "does this look like a real app" turned out to be
the font: ImGui's built-in default is a small bitmap font meant for debug
overlays, not a real UI. Loaded Segoe UI (regular + bold) from
`C:\Windows\Fonts\` -- safe to hardcode since this is a Windows-only app
and Segoe UI ships on every stock install; `AddFontFromFileTTF` returns
null rather than crashing if the path's ever wrong, with a fallback to
ImGui's built-in font. Section labels and collapsing-header titles now use
the bold variant via a small `EditorUI::sectionLabel()` helper. Also
tuned `ImGuiStyle` (rounding, padding, spacing) away from ImGui's very
utilitarian defaults.

**Verified:** full 34-check test suite still passes (none of these were
pure-logic changes worth new automated tests -- they're rendering/input/
visual fixes verified by building, running, and re-checking the geometry
sanity numbers, which were unaffected). Screenshots from the desk PC
confirmed the underlying bugs before each fix was written.

## Post-milestone-7 batch 3 — a self-inflicted regression, a real ImGui gotcha, bed save/load, and a move gizmo

More feedback from testing, including one bug in the PREVIOUS fix.

**Geometry-mode selection still invisible -- because the fix from batch 2
was never actually being triggered.** Batch 2 made `GeometryRenderer` bake
`selectionHighlightColor()` into the mesh, which was the right diagnosis
-- but the code that decides WHEN to rebuild that mesh only checked
`sceneDirty` (structural changes), not `selectionDirty` (pure selection
changes), because `selectionDirty` was deliberately kept cheap (highlight-
overlay-only) as a performance fix from the same batch. Selecting a path
while already in Geometry mode set `selectionDirty` but never touched
`sceneDirty`, so the newly-baked color never made it to the GPU until
something else happened to trigger a full rebuild. Fixed: `selectionDirty`
now also rebuilds `GeometryRenderer` specifically when Geometry mode is
active (Lines mode still stays cheap, since it never needed the bake in
the first place). A reminder that "fix the root cause" and "wire the fix
into every place that needs to run it" are two different steps.

**Pan direction: only the vertical axis was still backwards.** flipped
just the `up` term's sign a second time, left the horizontal term as
batch 2 left it.

**Objects panel: Delete wasn't deleting anything -- a real Dear ImGui
gotcha.** The Name cell's `Selectable` used
`ImGuiSelectableFlags_SpanAllColumns`, which extends its hit-test region
across the ENTIRE row, including the Color/Reorder/Link/Delete cells drawn
AFTER it. That silently intercepted clicks meant for those later widgets
-- clicking Delete just re-triggered row selection instead. This is a
documented Dear ImGui table interaction, not a one-off mistake. Fixed by
dropping `SpanAllColumns` (the row is no longer clickable everywhere to
select the object, only on the name text itself -- a fair trade for the
other buttons actually working). Also moved the Visible checkbox to the
leftmost column per feedback, matching how most 3D-tool outliners put
visibility first.

**Bed save/load (`io/BedIO`):** a small plain-text `key value`-per-line
format, deliberately NOT the general project format (that's milestone 12).
`ui/FileDialog` gained `showSaveBedDialog`/`showOpenBedDialog`
(`GetSaveFileNameW`/`GetOpenFileNameW`), wired to new Save/Load buttons in
the Bed panel.

**Move gizmo (`editor/Gizmo` + `render/GizmoRenderer`):** the active
object always shows red/green/blue arrows at its pivot; dragging one
translates the object along that axis. The core math is the standard
"closest point between two 3D lines" technique -- `unprojectRay()` turns
the 2D cursor into a 3D ray, `closestPointOnAxisToRay()` finds where that
ray comes closest to the axis line (a ray generally never exactly touches
a 1D line in 3D, so this is the well-defined thing to solve for instead).
Because the axis direction is unit length, the returned parametric
distance IS the translation amount directly -- no extra unit conversion.
The one subtlety worth remembering: the axis's reference point must be
captured ONCE at drag start and held fixed for the whole drag, never
recomputed from the very value being dragged -- doing that would make each
frame's distance relative to a different basis point, so deltas between
frames would be comparing incompatible numbers. Gizmo-vs-selection input
priority: a click is tested against the three arrows (screen-space
point-to-segment, same technique as path picking) BEFORE falling through
to click/marquee path selection, so grabbing an arrow never accidentally
also selects a path underneath it.

**Verified:** added 20 more checks (54 total, up from 34): `testBedIO()`
(save/load round-trips every field exactly; loading a missing file
returns false rather than silently leaving garbage) and `testGizmoMath()`
(a hand-derived orthographic-projection case: a ray through a known screen
column travels along Z only and its closest point on the world X-axis
lands at exactly the expected world-space X; a ray parallel to the axis
correctly reports "no unique closest point" instead of returning nonsense;
axis picking finds the near arrow and correctly finds nothing when the
point isn't near any of them). All pass. Full app builds and runs cleanly.

## Post-milestone-7 batch 4 — marquee tolerance, culling, gizmo redesign, milestone 9 (SRC export)

The biggest batch yet, triggered by a real production file (a 24,268-line
Eidos-sliced KUKA SRC, `LEG_INNER_REV_A`) the user shared partway through --
used as a genuine stress test throughout, not just a stated goal.

**Marquee "touches" instead of "fully inside":** `pickPathsInRect` used to
require a path's MIDPOINT inside the drag rectangle. Replaced with
Liang-Barsky segment-vs-rectangle clipping -- a path only grazing a corner
of the marquee is now selected, matching how marquee-select conventionally
feels.

**A genuine winding bug, found by trying to add backface culling.**
Deriving the box mesh's face normals by hand (to check whether
`GL_CULL_FACE` could safely be enabled) turned up that 5 of 6 faces had
inverted winding -- likely from an index-order mistake when the mesh code
was first written, silently harmless only because culling was disabled.
Rather than re-deriving the fix by hand (proven error-prone by the
original bug), rewrote triangle emission around a self-correcting
`appendQuad()`: it computes the actual face normal from the vertices and
picks winding order to match a stated expected-outward direction, so
correctness no longer depends on getting index order right by hand.
`RenderSettings::backfaceCulling` (default on) now safely hides the tube's
inside surface in Geometry mode -- "depth culling for a better preview."

**"Select backfacing geometry" toggle:** picking now prefers the
camera-nearest candidate among on-screen-plausible matches by default,
with a toggle to fall back to pure 2D-nearest (letting a click reach
something behind/inside other geometry). A screen-space approximation of
occlusion, not a real depth-buffer test -- deliberately: it only compares
candidates already close enough on screen to plausibly be the intended
click, which is cheap and sufficient without adding a whole depth-readback
pipeline.

**4x MSAA** (`GLFW_SAMPLES`) for smoother line/edge rendering -- fixed at
window-creation time; a runtime toggle would need to destroy and recreate
the GL context in vanilla GLFW, out of scope here.

**Gizmo redesign -- fixes the reported "can't see it" bug AND adds
Start/End/Whole path editing.** Root cause of the visibility bug: the
gizmo drew at the object's raw `Transform.x/y/z`, which is `{0,0,0}` for
any freshly-loaded file whose own coordinates are far from local-space
origin (exactly the real KUKA file's case -- 300-2700mm range while
Transform stays at zero) -- the gizmo was rendering in genuinely empty
space, off-screen relative to the actual geometry. Fixed by
`computeGizmoOrigin()`: the gizmo now always sits at the world-space
centroid of whatever it would actually move, never the raw pivot. This
also unlocked the requested feature: `GizmoTargetMode` (Object/Start/End/
Whole) lets the gizmo edit the CURRENT PATH SELECTION directly -- Start/
End move just one endpoint of each selected path (can break connectivity
with an unselected neighbor, an accepted trade-off, same as moving one
vertex of a polyline in any curve editor), Whole translates each rigidly.
Dragging math converts the gizmo's world-space delta back into each path's
local space via a new `inverseTransformDelta()` (Transform.h) -- the
inverse of `applyTransform()`'s rotation+flip, deltas don't need the
translation component.

**A/B/C orientation was being silently dropped.** The parser had
`kARe`/`kBRe`/`kCRe` regexes defined but never used -- `Path` had nowhere
to put the values. Added `Path::a/b/c` and wired the parser to actually
capture them. This wasn't cosmetic: a real KUKA LIN motion needs full pose
to be a valid, safe command -- an exporter built without this would have
silently produced incomplete robot programs.

**Milestone 9 -- SRC export, `editor/SrcExporter`.** Deliberately PATCHES
the original source lines rather than regenerating them from the model.
The real file has fields the model still doesn't fully capture (external
axes E1-E6, the `C_VEL` continuous-blend flag) plus custom
interrupt/safety logic, disclaimers, and comments -- regenerating from
scratch would silently drop all of it. Patching means anything not
specifically touched is preserved byte-for-byte, because it's literally
never read past being copied. Two things get patched:
- A motion line's X/Y/Z, replaced via `applyTransform()`'s current result,
  only when the value actually changed (an untouched line stays
  byte-identical).
- `$VEL.CP` insertions, computed by walking paths in order and tracking
  TWO separate timelines -- what the untouched original lines already
  establish at each point, versus what's actually in effect in the edited
  output. Only insert when they diverge. Getting this right took a real
  bug fix: the first version compared against "whatever we last declared"
  instead of "what this path's own original line establishes," which
  caused it to insert a spurious correction at every ALREADY-EXISTING
  speed change in the file (a natural transition the original already
  handles correctly needs no insertion) -- caught immediately by the test
  suite (an untouched round-trip should produce zero insertions, and
  didn't). The fixed two-timeline model produces exactly the right
  insertions: none for an untouched file, an override + automatic restore
  for a single-path speed edit (the restore isn't special-cased -- it just
  falls out of the same divergence check once the override region ends).

**Layer actions** (`model/LayerAction`): operator-inserted KRL commands at
the start of a specific layer (HALT, part cooling on/off, or custom text).
The actual command text is always operator-supplied, never hardcoded --
this app has no way to know a given robot cell's real I/O mapping (which
`$OUT[n]` controls cooling on THIS system), and guessing would risk
generating a command that does the wrong thing on a real machine. Presets
in the UI pre-fill common boilerplate as a starting point; the operator
must confirm/edit the real command before export.

**Verified, extensively, including against the real file:**
- 27 new unit tests (101 total, up from 74): A/B/C capture, transform-delta
  inversion, gizmo-origin modes, and four `SrcExporter` scenarios (byte-
  identical untouched round-trip, transform patch, speed override +
  auto-restore with exact insertion count, layer action insertion).
- Debug-only `GCODEFORGE_TEST_FILE` env var added to `main.cpp`, timing
  parse/rebuild/export against a real file without needing the UI.
- Real-file finding: Debug-build parsing took 21.4s (MSVC's Debug STL,
  especially `std::regex`, carries heavy iterator-checking overhead) vs.
  **641ms in the actual shipped Release build** -- a reminder to always
  benchmark the configuration that ships, not the one used for day-to-day
  building. Rendering scaled cleanly too: 23,991 line segments / 191,684
  geometry triangles built in single-digit milliseconds.
- Real-file round-trip: exporting the untouched 24,268-line file produced
  0 patched coordinates, 0 inserted speed lines, 0 inserted layer actions,
  and output byte-identical to the original -- the strongest available
  evidence the patch-based design actually holds up on production data,
  not just the hand-written test snippet.

## Post-milestone-9 feedback batch -- selection, dragging, framing, lighting

A round of fixes/features driven directly by the operator testing real
builds on real hardware (desk PC, RTX 4080) and reporting bugs by photo.

**Marquee selection now does real rectangle-segment intersection.**
Previously a path counted as "inside" the drag rectangle only if its
*midpoint* fell inside it -- a path that crossed the rectangle without its
midpoint landing inside was missed. Replaced with Liang-Barsky
segment-vs-rectangle clipping (`editor/Picking.h`), so a path is selected
if any part of it intersects the rectangle, matching every other editor's
marquee-select behavior.

**Select-backfacing toggle.** `RenderSettings::selectBackfacing` -- off
(default) prefers whichever path is nearer the camera when candidates
overlap on screen, matching what you'd expect to click; on, picking falls
back to pure 2D screen-space nearest, letting the operator grab geometry
that's behind something else without having to rotate the view first.

**Backface culling for Geometry mode preview.** Requested as "depth
culling for better preview" -- interpreted as GPU backface culling
(`RenderSettings::backfaceCulling`, `glCullFace(GL_BACK)`), which single
handedly surfaced a real mesh-winding bug: 5 of the 6 box faces in the
bead mesh had inverted normals from a hand-derivation error, invisible
under normal double-sided rendering but immediately obvious with culling
on (whole faces vanished). Fixed with a self-correcting `appendQuad()`
that computes the actual face normal from vertex positions and picks
winding order to match a stated expected-outward direction, instead of
trusting hardcoded index order -- can't silently drift out of sync with
the mesh topology again.

**4x MSAA** (`GLFW_SAMPLES=4` before window creation) for smoother line
edges, per "sampling options for better line drawing."

**Gizmo redesign -- see milestone 9's entry above** for the full
writeup (visibility root cause + Start/End/Whole target modes); listed
here again only because a follow-up fix landed in this same batch, below.

**Connected dragging.** After Start/End/Whole gizmo editing shipped, the
operator found that moving a path's endpoint left it visibly detached
from its neighbor (screenshot: a gap where two paths used to touch).
`editor/ConnectedDrag.h/.cpp`'s `buildDragSnapshots()` fixes this by
propagating the drag delta one hop to any immediately-adjacent,
*unselected* path whose touching endpoint currently coincides
(`glm::length(prev.to - p.from) < 1e-4`) with the endpoint being moved --
so dragging the middle path of a connected chain pulls both neighbors'
touching ends along with it, while a genuine gap (paths that were never
actually touching) correctly does NOT get pulled together. 9 new tests
cover Whole/Start modes and the no-pull-on-a-real-gap case.

**Gizmo made thicker** (`glLineWidth` 2.0 -> 5.0 in `GizmoRenderer`) --
straightforward legibility fix, no design questions involved.

**Outline selection -- two attempts, first one shipped broken.** Attempt
1: draw a screen-space-wide line along each selected path's centerline
*before* the real geometry, relying on ordinary depth testing to let only
the wide line's edges show through as a border (a real, standard trick --
and still in use today for Lines-mode and travel-path highlighting, where
it's provably correct). Reported broken by the operator with a photo:
"the highlight is only from one side, not the whole line." Root cause:
that trick only produces a symmetric outline for something with no depth
extent perpendicular to the view (a flat line in Lines mode). A Geometry
mode bead is a 3D tube -- a fixed-pixel-width line drawn along ITS
centerline pokes out by different, view-angle-dependent amounts on each
side, so it reads as one-sided highlighting rather than a clean outline
from most angles. Attempt 2 replaced it with the standard "inverted hull"
technique for Geometry mode specifically: build a second mesh containing
only the selected (and connectivity-pulled-in) paths, enlarged by a fixed
margin, and draw it FIRST with **front-face culling** (`GL_CULL_FACE` +
`GL_FRONT`) so only its far/back faces render; the real mesh, drawn next
and always closer to the camera on its own front surface, naturally
occludes the shell's center via normal depth testing, leaving only a rim
visible right at the silhouette edge -- angle-independent by construction,
unlike a screen-space line. `GeometryRenderer` gained a whole second
VAO/VBO/EBO for this outline mesh; `SelectionHighlightRenderer` now skips
print-type paths entirely in Geometry mode (the outline mesh replaces
that job there) while still using the original wide-line technique for
Lines mode and for travel paths in both modes. Verified via an automated
startup sanity check: selecting every print path on the sample object and
rebuilding produces exactly 144 outline triangles (matching the main
mesh's own triangle count for a fully-selected object) with
`glGetError() == GL_NO_ERROR`.

**View/frame keybinds.** `editor/Framing.h/.cpp`'s `computeFrameBounds()`
computes a bounding sphere -- of the current selection if non-empty,
falling back to the whole scene otherwise -- and `Camera::frameBounds()`
repoints the orbit target and adjusts zoom to fit it with a 1.3x margin.
Wired to key **F** (frame). T/P/U map to existing camera preset/projection
calls (Top view, Perspective, Orthographic). Note: the operator's request
listed "F" for both frame and front view in the same sentence -- resolved
by keeping Front on its existing key (2) and its UI button, since F=frame
matches the Maya/Blender convention and was the first meaning given.

**Multi-light "Environment" system**, in the Bed panel per "put it in the
bed environment rollout." Replaced the single hardcoded `lightDir` with
`LightingSettings` (`include/render/LightingSettings.h`): a
`std::vector<Light>` of up to `kMaxLights = 4`, each with a direction,
color, and enabled flag, defaulting to one light matching the old
hardcoded value so existing files look identical until the operator
actually opens the new panel. `MeshShader`'s fragment shader now loops
over `uLightDirs[4]`/`uLightColors[4]`/`uLightCount` instead of a single
direction uniform. `GeometryRenderer::draw()` takes a `const
LightingSettings&` and uploads only the *enabled* lights each frame (a
disabled light isn't sent at all, rather than sent with a zero color).
New Bed-panel section: per-light direction sliders (-1..1 per axis),
color picker, an enable checkbox, and Add/Remove buttons (remove disabled
below one light, add disabled at the 4-light cap). Lighting is a
per-frame shader uniform, never baked into any mesh, so changing it needs
no scene rebuild -- unlike almost everything else in the Bed/Editor
panels, it doesn't touch `bedDirty`/`sceneDirty` at all.

**Verified:** all 101 existing tests still pass (no regressions); startup
sanity checks (Geometry mesh, outline mesh) both report `glGetError=0`
after the lighting-signature change; full Debug rebuild of both
`gcode_editor` and `parser_smoke_test` succeeded cleanly.

## Selection style dropdown + three-point lighting preset

Immediate follow-up to the outline-selection work above. The operator's
own framing of the request was sharp enough to design directly from:
"flipped normals, over-extruded" was their read of the inverted-hull
outline on a screenshot, and their suggested alternative -- brighten the
selected thing over time, keep the real geometry dark so it stays visible
-- described a completely different technique (per-vertex color tinting,
no second mesh at all), not a fix to the existing one. Rather than
guessing which one they'd actually prefer, both now exist behind a
**Selection style** dropdown (View panel, Geometry mode section) so it's
a one-click A/B instead of another round-trip.

- **`RenderSettings::SelectionStyle`** (`Outline` / `Pulse`), defaulting
  to `Outline` (unchanged prior behavior).
- **`Outline`** is exactly the existing inverted-hull technique from the
  previous entry, now gated behind this enum instead of always running.
- **`Pulse`** (the operator's suggestion): `MeshVertex` gained a 4th
  attribute, `selected` (0.0/1.0 per vertex, set at `rebuild()` time from
  `object.selectedPaths`), uploaded to a new vertex attribute location 3.
  The mesh fragment shader now takes `uSelectionStyle`, `uTime`, and
  `uHasSelection` uniforms: when the style is Pulse and something is
  actually selected, selected vertices smoothly blend toward white on a
  `sin(uTime)`-driven cycle, while everything else in Geometry mode is
  multiplied down to 25% brightness -- exactly the "selected glows, the
  rest goes dark so the glow reads clearly" effect requested. No second
  mesh, no extra draw call -- purely a per-frame uniform + an existing
  per-vertex attribute, which is why `uHasSelection` matters: without it,
  an empty selection (every vertex's `selected` = 0) would incorrectly
  dim the *entire* scene rather than just not-highlighting anything.
  `GeometryRenderer::draw()` now takes the active `SelectionStyle` and the
  current time (`glfwGetTime()`, threaded from `main.cpp`) to drive this.
- **Three-point lighting preset**, added to the Environment/Lighting
  section from the previous entry per a follow-up request ("make three
  light setup option"): one button sets `lighting.lights` to the classic
  photography/film rig -- a bright key light, a dimmer fill from the
  opposite side (softens the key's shadows without erasing them), and a
  subtle rim/back light for separation from the background. Doesn't
  disable manual per-light editing afterward -- it's a starting point,
  not a locked mode.

**Verified:** all 101 tests still pass; full Debug rebuild clean; startup
Geometry/outline sanity checks still report `glGetError=0` (Outline style
is still the default, so this is confirming the new gating didn't break
the existing path, not just the new one).

## More selection styles, color-by-object fix, layer range-select, bead UX, bed heightmap

Another operator-feedback batch, the biggest so far -- one real bug report
(color-by-object), one UX request (layer range-select, bead defaults/drag),
and one genuinely new feature (bed heightmap).

**Stripes and Wireframe selection styles, and a real fix to Pulse.** The
operator's read of Pulse from a screenshot -- "weird" -- pointed at a real
bug, not just taste: the tinted color was still being multiplied by
`lightSum` afterward, so any face angled away from every light stayed
mostly at the 0.35 ambient floor regardless of how bright the pulse tint
was -- half the selected tube visibly glowed and half didn't, depending on
its facet's normal. Fixed by making highlighted fragments **emissive**
(skip the lighting multiply entirely) in every animated style, so the
highlight color is what actually reaches the screen. Two new styles
added alongside the fix, per "make multiple versions": **Stripes**
(diagonal black/white hazard-tape bands scrolling across selected
geometry, driven by world position + `uTime` so the motion reads the same
from any angle -- about as hard to confuse with lit geometry as this gets
without an image texture) and **Wireframe** (the existing outline mesh,
reused, drawn as `GL_LINE` instead of filled front-culled triangles -- a
bright cage around the selection). `MeshVertex` gained `vWorldPos` as a
shader varying (free -- vertex positions are already world-space, no
model matrix exists in this app) to drive the stripe pattern. Selection
style dropdown now lists all four.

**"Color mode: Object" fix.** Root cause: every `SceneObject` was created
with the exact same hardcoded default color (`SceneObject.h`), so loading
a second file made both objects render identically in Object mode until
the operator manually recolored one via the object list's swatch --
indistinguishable objects looks exactly like "doesn't work" even though
the color-mode *logic* itself (`pathColor()`'s `Object` case) was already
correct. Fixed in `loadFileIntoScene()` (`main.cpp`): each newly-loaded
object now gets the next color in the shared palette, indexed by load
order, so Object mode distinguishes objects out of the box. Still just a
starting point -- the swatch can still override it per object.

**Layer table shift-click range-select.** Previously shift-click just
ADDED the one clicked layer (Add compose mode), same as a plain click
except non-destructive to the existing selection. Now: `EditorUI` tracks
`layerSelectionAnchor_`, the last layer clicked WITHOUT shift (plain or
ctrl). A shift-click computes the full inclusive range between the anchor
and the clicked layer and adds every path in every layer in that range --
"click layer 3, shift-click layer 37, get 3 through 37" exactly as
requested. Ctrl-click (subtract) still operates on just the one clicked
layer and also moves the anchor, matching the usual file-explorer
convention where any non-shift click resets the range starting point.

**Bead defaults changed to 7mm width / 3mm height** (was 8/4mm),
`RenderSettings::beadWidthMm/beadHeightMm`. Both controls switched from
`SliderFloat` to `DragFloat` -- click-and-drag the number itself to scrub
the value (still double-click/Ctrl+click to type an exact one), instead
of needing to land the cursor on a thin slider track.

**Bed heightmap -- new feature.** The request: enter real bed-elevation
measurements taken every 10cm and see them as a heatmap, to catch a
warped bed before it ruins a print.
- `model/BedHeightmap` (core, no GL dependency): a row-major grid of
  elevation samples (mm), `spacingMm` apart in both axes, sized from the
  current bed's width/depth via `resizeToBed()` (`floor(extent/spacing)+1`
  per axis, so both edges always get a sample point). Resizing (bed size
  or spacing change) preserves existing values BY GRID POSITION wherever
  the old and new grids overlap, instead of discarding entered
  measurements on every edit.
- `render/BedHeightmapRenderer`: one quad (two triangles) per grid cell,
  Z-offset by the measured elevation at each corner, colored via a blue
  (low) -> green (mid) -> red (high) heatmap ramp normalized to the
  largest elevation magnitude currently entered -- reads at a glance
  without needing a legend. Deliberately reuses `GeometryRenderer`'s
  `MeshVertex` layout and mesh shader (position/normal/color/selected,
  `selected` always 0 here) rather than writing a near-duplicate shader
  just for this.
- Bed panel gained a "Bed Heightmap" section: spacing control, "Resize
  grid to bed" / "Reset all to 0" buttons, a "Show heatmap" toggle, and a
  scrollable table of `DragFloat` cells (one per grid point, row order
  matches Top-view orientation) for entering the actual measurements.
- `io/BedIO`'s save/load format extended to include the heightmap
  (spacing, visibility, dimensions, and every value, one row per line) --
  measurements taken on a real bed are exactly the kind of thing that
  should survive a session, saved together with the bed they describe.
  5 new round-trip tests cover every field including the full value grid.

**Verified:** 106 tests pass total (5 new BedIO heightmap tests); full
Debug rebuild clean; startup sanity checks extended with a bed-heightmap
check (11x11 grid at the default 1000mm bed / 100mm spacing -> 200
triangles, `glGetError=0`) alongside the existing Geometry/outline checks.

## Heightmap Y-flip fix, undoable selection, heightmap Save/Load convenience

The operator tested the heightmap feature immediately (screenshot of the
Bed panel with real values entered) and found a real orientation bug
within minutes.

**Heightmap Top-view Y-axis flip -- real bug, found via first real use.**
Reported precisely: entering 6.66 in the grid table's top-left cell and
-6.66 in its top-right cell rendered them mirrored top-to-bottom from
where they were expected. Root cause, found in `Camera::viewMatrix()`/
`orientation()`: the Top preset's "up" vector is derived from the SAME
orbit quaternion as every other view (`q * kBaseUp`), and at Top's
yaw=0/pitch=+89.5 deg, that resolves to world **-Y**, not +Y -- i.e. this
camera's Top view has -Y pointing up on screen, the opposite of the usual
CAD convention I'd assumed when writing the heightmap table's row order
(`EditorUI.cpp`'s heightmap grid deliberately drew row=rows-1, the +Y
edge, as the table's first/top-displayed row, intending it to match "up
on screen in Top view"). The X axis was independently verified correct
(rotation around the X axis doesn't move a vector already along X, so
"right on screen" is exactly world +X regardless of pitch) -- this was a
single-axis bug, not a wholesale mirroring. Fixed by reversing the loop
so row 0 (the -Y edge) is what's drawn first/top in the table, matching
what Top view actually shows.

**Selection changes are now undoable.** Previously `UndoStack` was wired
to every scene-mutating action (transform, speed, color, layer actions,
gizmo drags) but not to selection changes -- clicking a path, marquee-
select, layer-table clicks/range-select, selection-group apply, "Select
all visible", and "Clear" all mutated `selectedPaths` directly without
recording anything. Since `UndoStack` already does whole-`Scene` value
copies (and `SceneObject::selectedPaths` is plain data, copied along with
everything else), no new storage or copy logic was needed -- every one of
those call sites just needed a `snapshotBeforeChange()` call added before
the mutation, the same one-line pattern already used for every other
discrete action in the codebase.

**Heightmap Save/Load convenience buttons.** The Bed panel is now long
enough (Environment/Lighting section plus a 100+ field measurement grid)
that the existing Save Bed/Load Bed buttons, up in the "Bed size"
section, were easy to lose track of while scrolled down editing
measurements. Duplicated the same two buttons (same request flags,
already wired in `main.cpp` to save/load the heightmap alongside the rest
of the bed) directly below the heightmap grid controls.

Two more requested items turned out to already be correct/implemented:
elevation was already moving each grid point's Z position (not just its
color) in `BedHeightmapRenderer::rebuild()`, and the heatmap's color
ramp already mapped high values to red / zero to green / negative to
blue as specified -- both confirmed by re-reading the existing code
against the operator's description rather than needing a new change.

**Verified:** all 106 tests still pass; full Debug rebuild clean; startup
sanity checks (Geometry, outline, bed heightmap) all still report
`glGetError=0`.

## Stripes as default, panel collapse toggle, bed heightmap grid redesign

Another operator-feedback batch, plus a real bug caught by actually
running the test suite rather than assuming a refactor was safe.

**Stripes made the default selection style, and no longer dims the rest
of the scene.** `RenderSettings::selectionStyle` now defaults to
`Stripes` (was `Outline`). The mesh fragment shader's "dim everything
else" branch, previously shared by both Pulse and Stripes, is now Pulse-
only -- Stripes leaves unselected geometry at normal lit brightness
("don't turn the light off"), relying on the moving stripe pattern alone
to read as unmistakable.

**Panel collapse toggle.** A "Hide panels"/"Show panels" button lives
directly in the menu bar (not a floating window of its own, so it's
always reachable regardless of state). When collapsed, `EditorUI::draw()`
returns immediately after drawing the menu bar, skipping the Editor and
Bed windows entirely for an unobstructed view of the viewport.

**Multi-monitor / detachable panels -- investigated, not implemented
this round.** Dear ImGui's docking branch supports exactly this
(`ImGuiConfigFlags_ViewportsEnable` lets any window be dragged out into
its own OS-level window, including onto another monitor) and the
existing GLFW/OpenGL3 backends already have full support for it built
in -- no backend rewrite needed. The blocker is that this repo pins
ImGui to the `v1.91.1` tag on the mainline branch (`CMakeLists.txt`);
docking/viewports live on a separate branch with its own API surface,
so adopting it means a real dependency swap and a full UI re-test, not
a quick flag flip. Worth doing as its own focused change rather than
folding into this batch.

**Bed heightmap grid redesign: columns/rows instead of spacing.** Per
the request ("bed 100cm, 10 columns, 5 rows"), `BedHeightmap` no longer
derives its grid from a spacing value -- `cols`/`rows` are now the
operator-set source of truth directly (Bed panel gained "Columns"/"Rows"
integer fields, replacing the old "Spacing (mm)" + "Resize grid to bed"
button), with X/Y spacing simply derived as `bedWidthMm/(cols-1)` at
render/save time. `BedHeightmap::resizeToBed()` was replaced with
`resize(cols, rows)`, resizing directly rather than being driven by bed
dimensions. `io/BedIO`'s save format changed to match: each grid point
now gets its own `heightmapPoint <localX> <localY> <elevationZ>` line
(local bed-relative X/Y, computed at save time, plus the raw measured Z)
instead of the previous compact `heightmapRow <v0> <v1> ...` blocks --
explicit per-point positions make the saved file self-describing and
reconstructible without knowing the grid-generation formula, matching
"if I save it I get a position of the whole bed and the position of each
point."

**Real bug found and fixed: an out-of-bounds read that manifested as an
infinite hang, not a crash.** While updating the round-trip test for the
new API, `parser_smoke_test.exe` started hanging forever partway through
-- three separate runs each got stuck and had to be killed via
`taskkill`. Root cause: `BedHeightmap`'s new defaults (`cols=10, rows=5`,
chosen to match the "10 columns, 5 rows" example in the request) left
`elevationsMm` at its OWN default -- an empty vector -- so a freshly
default-constructed `BedHeightmap` had `cols`/`rows` claiming 50 points
while the backing vector held zero. `resize()`'s "preserve existing
values by grid position" copy loop trusted that invariant and read
`elevationsMm[row*cols+col]` out of bounds. In a Debug build, MSVC's
Debug STL raises an assertion dialog on that -- which, with no console
attached to display it, blocks the process forever waiting for a click
that can never come. That's indistinguishable from an infinite loop from
the outside, which is exactly what it looked like until the process list
was checked directly (`tasklist`/`taskkill`) and the hang point was
narrowed down by adding unbuffered stdout to the test binary and
re-running under a hard timeout. Fixed at the root: `elevationsMm`'s
default member initializer now explicitly sizes itself to `cols*rows`
(legal C++ -- member initializers run in declaration order, so `cols`/
`rows` are already set when this one runs). `resize()` also got a
bounds check against the ACTUAL current vector size as defense in depth,
in case the invariant is ever violated again some other way.

**Verified:** all 106 tests pass (confirmed via a hard-timeout run after
the fix, not just "it returned"); full Debug rebuild clean; startup
sanity checks report a 10x5/72-triangle default grid with `glGetError=0`.

## Path splitting

First of four larger requested features (path split, object linking, bed-
based speed/Z conform, interleaved multi-object print order), tackled
one at a time since the remaining three all touch the SRC exporter --
the code that writes real robot motion commands.

**`editor/PathSplit.h/.cpp`, `splitSelectedPaths(SceneObject&)`.** For
each selected path A->B: the path itself is shortened in place to
midpoint->B (keeping its number, srcLine, and layer, so its selection
membership and layer-table entry stay valid), and a NEW path A->midpoint
is inserted into the paths vector immediately before it, with a fresh
unique number. Requested specifically for giving half of a long
travel/print move its own speed override.

**The real design problem: the new half doesn't correspond to any line
in the original file.** `SrcExporter`'s whole model is "patch existing
lines, never invent one" -- every prior insertion (speed lines, layer
actions) was inserted RELATIVE to an existing path's `srcLine`, but a
split path has no `srcLine` of its own to patch OR anchor to. Solved
with a new `Path::cloneTemplateSrcLine` field: the synthetic half points
at its sibling's real `srcLine`, and `SrcExporter` clones that line's
FULL text (motion command, E1-E6, C_VEL, trailing comment -- everything
`replaceAxisValue()` already leaves alone) with just its own X/Y/Z
substituted in, inserted immediately before the sibling's real line --
so a split reads in the export as two consecutive motion commands with
the same shape as the original one, not a mystery line.

**Rewrote `buildExportedLines()`'s speed-insertion loop as a single pass
in file order instead of three independent passes.** The old structure
(patch coordinates, THEN layer actions, THEN speed tracking, each its
own loop over `object.paths`) worked because every path had a real
`srcLine` to key insertions off of. A synthetic path breaks that: its
speed needs tracking in the SAME two-timeline model as any other path
(so overriding its speed still auto-restores afterward), but it has no
original `$VEL.CP` line of its own to ever "naturally assert" a speed --
it just inherits whatever's currently in effect. Introduced
`exportTargetLine()` (a path's own `srcLine` if it has one, otherwise
its `cloneTemplateSrcLine`) and merged the speed-tracking walk with the
new-line-synthesis step into one pass over `object.paths` in vector
order (which already IS final file order, since `splitSelectedPaths()`
inserts the synthetic half directly before its sibling) -- this keeps
relative ordering correct (speed line before its motion line, both
before the next path's content) without a redesign of the insertion
mechanism itself, and produces byte-identical output to before on any
file with no split paths (verified by the existing round-trip tests
still passing unchanged).

**6 new tests:** model-level (`testPathSplitModel`) verifies path count,
which half keeps the original number/srcLine, the new half's
from/to/number/srcLine/cloneTemplateSrcLine; export-level
(`testPathSplitExport`) verifies the exported file actually gains a new
line (not silently dropped), and that re-parsing the export finds a
motion line ending exactly at the midpoint AND one still reaching the
original endpoint.

**UI:** "Split selected" button next to Select all/Clear in the Layers
panel, disabled with no selection, undoable via the same
`snapshotBeforeChange()` pattern as every other discrete edit.

**Verified:** all tests pass including the 6 new ones; full Debug
rebuild clean; startup sanity checks still report `glGetError=0`.

## Object linking + Bake Links to Travels (milestone 10)

Second of the four larger requested features. This closes out
milestone 10, which had been sitting as a data-structure-only stub since
early in the project (`Scene::objectLinks`/`toggleLink()` existed, and
the object list's "Link->next" checkbox already toggled it, but nothing
ever consumed the link -- no preview, no way to make it permanent).

**`editor/ObjectLinking.h/.cpp`:**
- `computeLinkPreviews(const Scene&)` -- for each pending pair in
  `scene.objectLinks`, computes a world-space line from the FROM object's
  LAST path's end point to the TO object's FIRST path's start point
  (both run through `applyTransform()`, so each object's own position/
  rotation/flip is correctly accounted for). Silently skips a pair whose
  object was deleted or has no paths -- a link toggle can outlive the
  object it names.
- `bakeLinkToTravel(Scene&, fromId, toId)` -- converts one pending link
  into a REAL, permanent path. Unlike path splitting's synthetic
  `cloneTemplateSrcLine` approach (a new line inserted relative to an
  EXISTING sibling), a baked link has no existing sibling in the same
  object to anchor to -- it's genuinely new content at the very end. So
  this generates a proper `"LIN {X ..,Y ..,Z ..}"` source line directly
  (matching `SrcParser`'s expected format exactly) and inserts it into
  `sourceLines` right after the from-object's current last path's line,
  then appends a matching `Travel` `Path` with a REAL `srcLine` pointing
  at it -- not synthetic at all once baked, indistinguishable from a path
  that was actually parsed from the file to every downstream system
  (export, further edits, even a future split). Needed a new
  `inverseApplyTransform()` in `model/Transform.h` (the missing
  full-point inverse -- `inverseTransformDelta()` only ever handled
  direction deltas, not points with translation) to convert the TO
  object's world-space start point into the FROM object's own local
  space for storage.

**`render/LinkPreviewRenderer`:** one `GL_LINES` segment per pending
link, bright magenta (outside the normal palette, matching
`SelectionHighlightRenderer`'s "make it unmistakable" convention) so a
procedural preview never reads as a real travel move. Drawn regardless
of Lines/Geometry mode.

**UI:** "Bake links to travels" button under the object list, shown only
when `scene.objectLinks` is non-empty, converts every currently-pending
link in one click (undoable).

**4 new tests:** `computeLinkPreviews()`'s world-space math (including a
transformed second object, to catch a preview that silently ignored the
target object's own placement), and a full `bakeLinkToTravel()` round-
trip -- path count, source-line count, the baked path's type/srcLine,
its world position, AND that it survives a real export + re-parse cycle
(the actual point of giving it a real `srcLine` instead of a synthetic
one).

**Verified:** all tests pass (one test assertion of my own was wrong on
the first run -- comparing two unrelated points' X coordinates -- caught
immediately by actually running the suite rather than eyeballing the
logic, fixed by comparing against the correctly-computed expected value
instead); full Debug rebuild clean; startup sanity checks still report
`glGetError=0`.

## Bed conform: Z/speed compensation from the bed heightmap

Third of the four larger requested features. Compensates a print for a
bed that isn't perfectly flat: "when Z is too high we increase the
speed, when it is too low we lower the speed," plus optionally shifting
each affected path's own Z to match the measured surface, both tapering
off over an operator-chosen number of bottom layers.

**`editor/BedConform.h/.cpp`:**
- `sampleBedElevation(heightmap, bed, worldX, worldY)` -- bilinear
  interpolation between the four grid points surrounding an arbitrary
  world-space XY, so a path doesn't need to land exactly on a measured
  point to get a sensible compensation value. Returns 0 for an
  unconfigured heightmap (cols/rows < 2) rather than asserting or
  dividing by zero.
- `applyBedConform(object, heightmap, bed, options)` -- for every PRINT
  path (travel paths skipped, their Z/speed aren't meaningful the same
  way) at a layer within `options.affectedLayers`, weighted by a linear
  taper (full effect at layer 1, zero by layer `affectedLayers + 1`):
  optionally shifts `Path::to.z`/`Path::from.z` by the sampled elevation,
  and/or sets `Path::speedOverride` to
  `effectiveSpeed() * (1 + weight * speedGainPerMm * elevation)`
  (clamped so compensation can never crush speed to zero or negative).
- **The connectivity subtlety that made this worth double-checking with
  a test:** two print paths that share a vertex (`next.from == cur.to`,
  same definition `GeometryRenderer` uses for its mitered-run merging)
  must still share it after conforming, or the mesh gets a visible gap.
  Solved by re-sampling and shifting BOTH endpoints of EVERY path
  independently from their own world position, rather than propagating a
  shift from one path to its neighbor -- since a shared vertex has
  identical world XY on both sides, sampling it twice (once as one
  path's `.to`, once as the next path's `.from`) is guaranteed to produce
  the identical elevation and therefore the identical new Z, with no
  explicit propagation logic needed. Verified directly: a 2-path
  connected run's shared midpoint gets the same Z from both paths.

**UI:** new "Bed Conform" section on the active object (Editor panel,
after Speed), with affected-layer count, Z/speed toggles, a speed-gain
slider, and an "Apply bed conform" button -- disabled with an explanatory
message when the Bed panel's heightmap has no valid grid yet.

**6 new tests:** `sampleBedElevation()`'s bilinear math (exact corner
values, center average, an axis-independence check using a heightmap
with a gradient on only one axis), and a full `applyBedConform()` pass
over a hand-built 3-path object verifying the Z shift, the connectivity
preservation described above, the exact speed-override formula, and that
a layer beyond `affectedLayers` gets zero effect on either Z or speed.

**Verified:** all tests pass; full Debug rebuild clean; startup sanity
checks still report `glGetError=0`.

## Mirror + interleaved multi-part printing (for cooling)

Last of the four larger requested features, and the one with the most
real-world manufacturing intent behind it: mirror a part N times, spread
the copies safely apart, and print them layer-by-layer in rotation so
each part gets real cooling time between its own layers -- then
physically cut the connecting travels apart afterward to get N finished
parts. The operator's framing: "this will help print multiple objects at
once with speed and allows the plastic to cool down from the heat, this
is a super important feature."

**`editor/KrlLineEdit.h/.cpp` (refactor first).** `SrcExporter.cpp` had a
private `replaceAxisValue()` for patching an axis value inside a KRL
motion line without disturbing anything else on it. The interleave
builder needs the exact same operation (cloning a source line's format
into a merged sequence), so it was extracted to a shared header rather
than copy-pasted -- two copies of "the one function that decides how we
rewrite robot motion coordinates" is exactly the kind of duplication
that silently drifts. Verified as a pure refactor: full suite re-run
before building anything on top of it.

**`editor/MirrorObject.h/.cpp`.** Copies an object, toggles
`transform.flipX`, and offsets `transform.x` so the mirror sits clear of
the original by a custom operator-set gap.

**A real placement bug, caught by a test rather than by a crashed
print.** The first version offset by the part's WIDTH (`maxX - minX`).
That's only correct when a part's local X starts at 0 -- and real KUKA
files don't work that way, their coordinates sit wherever the cell's
work envelope puts them (300-2700mm in the sample production file). The
test asserted the mirror's leftmost world point clears the source's
rightmost world point, and it failed immediately. Re-derived properly:
`flipX` negates local X, so `[minX, maxX]` becomes `[-maxX, -minX]`, and
requiring `(-maxX + mirrorX) - (maxX + sourceX) == gap` gives
`mirrorX = sourceX + 2*maxX + gap` -- `minX` drops out entirely, only
the far edge matters. On real hardware the original formula would have
placed the mirror overlapping the original.

**`editor/InterleavePrint.h/.cpp`.** Builds ONE merged, exportable
`SceneObject` that round-robins 2+ objects layer-by-layer (A1, B1, C1,
A2, B2, C2, ...). Design decisions that mattered:
- **Where the transition goes.** Per the operator's clarification, the
  jump between parts replaces the existing layer-to-layer travel rather
  than interrupting a print: each per-object layer segment is emitted
  ending at its last PRINT path, and the source layer's own
  leading/trailing travels are dropped (they only made sense within one
  object anyway).
- **Collision safety.** The transition is synthesized as THREE explicit
  moves -- straight up to a clearance height, across at that height,
  then straight down -- rather than one diagonal, which is what
  guarantees the crossing actually happens above every part for its
  whole length instead of cutting a diagonal through one. Clearance is
  computed as `highestWorldZ(objects) + operator margin`. Directly
  asserted in a test: every generated travel is either purely vertical
  or entirely at/above the safe height.
- **World-space baking.** Every path in the merged object has its
  coordinates run through its own source object's transform, and the
  merged object's own transform is left at identity -- sidesteps
  reconciling N different local spaces inside one combined object.
- **Uneven layer counts.** An object that runs out of layers simply
  drops out of the rotation; the rest keep interleaving, and once one
  remains it finishes normally. Tested with a deliberately truncated
  second object.
- Generated travels are tagged `; GCODEFORGE INTERLEAVE TRAVEL -- cut
  here after printing`, so the thing the operator physically cuts is
  labeled in the exported program.

**UI:** "Mirror the object" section (named per the operator's request)
with a copy count, a custom space-between-copies value, travel clearance
and travel speed, a "Mirror the object" button, and "Build interleaved
print." Mirroring also auto-chains each new copy to the one it came from
via `Scene::toggleLink()`, so the existing link previews and "Bake links
to travels" are already wired up for the whole row without ticking each
Link->next box by hand. Building the interleaved object hides the
sources rather than deleting them -- the merged object is what gets
exported, but silently destroying the originals would be a nasty
surprise.

**11 new tests:** mirror (flip, path count, name, empty selection, and
the no-overlap placement property that caught the bug above);
interleave (identity transform, print/travel presence, the
travel-safety property, that consecutive segments actually alternate
between parts rather than printing one part sequentially, and a full
export + re-parse round-trip); and uneven-layer handling.

**Verified:** all tests pass; Debug and Release both rebuild clean;
startup sanity checks still report `glGetError=0`.

## The joint-space start point: parsed, displayed, movable

The operator hit a real limitation while using the tool on a production
file: "analyse the eidos source file and find the first safe position,
it's a point I couldn't find with gcode editor."

**Why it was invisible.** The point is line 88 of the real file:

    PTP {A1 0.000, A2 -89.990, A3 99.400, A4 0.000, A5 -9.410, A6 0.000}

It commands six AXIS ANGLES, not a Cartesian position -- there is no X,
Y, or Z on that line at all. `SrcParser`'s motion handler gated every
path on `if (x && y && z)`, so this line matched as a motion command,
found no coordinates, and fell through to nothing. Not a rendering bug
or a picking bug: the data never entered the model. It's also the ONLY
joint-space move in all 24,268 lines, which is why it never came up
before -- everything else in the file is Cartesian `LIN`.

**Why Eidos emits it.** A Cartesian point is reachable by several
different arm configurations (elbow up/down, wrist flipped). Commanding
joint angles removes that ambiguity, so the arm always begins from one
known, repeatable posture before flying to the first Cartesian target.
A2 ~= -90 / A3 ~= +99 is the classic "arm upright, forearm forward"
ready pose.

**`model/StartPoint.h`** -- deliberately NOT a `Path`. A Path has
`from`/`to` in Cartesian space and participates in selection, speed
editing, layer detection, run/mesh building and export patching; a joint
pose has none of that and would have to fake all of it. Stored instead as
its own optional member on `SceneObject`, carrying the six angles, the
source line, and a display anchor.

**The honest limitation, encoded in the design:** the file states no
Cartesian position for this pose, and computing one needs the robot's DH
parameters, which the program doesn't carry. So `StartPoint::position` is
documented as a display ANCHOR, not a derived truth -- it defaults to the
program's first Cartesian point (where the arm is heading next, the most
meaningful proxy available) and the operator can move it. It's stored in
LOCAL space, so it rides the object's transform exactly like a path does
-- which is what makes the operator's actual question ("did my start
point just leave the bed when I moved the object?") answerable by
looking.

**A regex trap worth naming.** The existing `kARe` matches a bare `\bA`
for tool orientation. Reusing it on a joint line would happily match the
`A` of `A1` -- and on the Cartesian lines it would read `A 164.577` as if
it were `A1`. The new `kA1Re`..`kA6Re` include the digit, and a test
asserts A4 parses as 0.0 rather than picking up the Cartesian lines'
`A 164.577`.

**Display filters** (`RenderSettings::showPrintPaths/showTravels/
showStartPoint`), exposed as a "Display: Paths / Travels / Start point"
row in the View panel. Pure view filters -- a hidden category is simply
not uploaded to a vertex buffer; nothing is deleted and export is
untouched. Worth having independently of the start point: a real file is
mostly print paths, and the travels weaving between them bury the
geometry you're trying to inspect.

**`render/StartPointRenderer`** draws a 3D crosshair plus a small open
box in amber, at fixed WORLD size rather than screen size -- "is this
still inside the bed" is a world-space question, and a marker that held
constant pixels while zooming would make a point far outside the bed
look close to it.

**Editing** lives in the Transform panel (the natural home -- it's the
same problem as object placement): the joint angles shown read-only for
reference, and the anchor editable as WORLD X/Y/Z, converted in and out
of local space via `applyTransform`/`inverseApplyTransform`. Undoable via
the continuous-edit pattern. A "moved by hand" note plus a Reset button
distinguish an intentional edit from the auto-derived default.

**Export is deliberately untouched.** Moving the marker changes display
and planning only -- the original joint move is written back verbatim.
Rewriting a joint-space PTP as a Cartesian one would reintroduce exactly
the configuration ambiguity the joint form exists to remove, which is a
robot-safety decision, not a rendering one. The UI says so directly
("Display/planning only: export still writes the original joint move")
rather than leaving it to be discovered.

**Verified on the real production file, not just a synthetic snippet:**
`GCODEFORGE_TEST_FILE` now reports the parsed start point, and on the
24,268-line file it prints `srcLine=87 A1=0.000 A2=-89.990 A3=99.400
A4=0.000 A5=-9.410 A6=0.000` with the anchor at the true first Cartesian
point (X 291.12, Y 2027.09, Z 4.20) -- and that file still round-trips
`byteIdentical=yes`. 15 new tests (192 total), Debug and Release both
clean.

## Part cooling silently did nothing -- a real production bug

First real robot run of a GcodeForge-exported program: the motion was
correct, but part cooling never switched on.

**Root cause, and it was mine.** The "Part cooling ON" layer-action preset
inserted this text:

    ; TODO: set the correct output for this cell, e.g. $OUT[12] = TRUE

That leading `;` makes it a KRL **comment**. It exported cleanly, the
robot ran cleanly, and the cooling output was never touched. I wrote it
as a placeholder specifically to avoid guessing the cell's I/O map -- but
the failure mode was the worst possible one: silent, and
indistinguishable from success until you're standing at the machine
watching a part not get cooled.

**The mapping was in the file all along.** The Eidos program's own
shutdown block labels its outputs:

    ;AIR COMMAND
    $OUT[5]=FALSE
    ;ULTRARESPONSIVE MODE
    $OUT[9]=FALSE
    ;EXTRUDER MOTOR COMMAND
    $OUT[7]=FALSE
    ;HITT TURNING BED HEAT OFF
    $OUT[6]=FALSE

So cooling/air is `$OUT[5]` on this cell.

**Two fixes, because fixing only the preset would leave the trap open:**
1. Presets now emit real commands (`$OUT[5]=TRUE`), with the output index
   as an **editable field** rather than a hardcoded 5 -- I/O assignment is
   per-cell, and firing an arbitrary output on a different machine could
   do something genuinely unwanted, so the operator confirms it up front
   instead of discovering it.
2. **A layer action whose text is empty or entirely a comment can no
   longer be added.** The Add button disables and says why. This is the
   part that actually prevents a recurrence: any future placeholder,
   typo, or half-finished custom command that couldn't possibly do
   anything gets caught at entry rather than at the robot.

## Layer speeds, hover readout, travel editing, Tab shortcut

**Speeds in the layer table.** Two new columns: a distinct-speed COUNT and
the min-max RANGE, with the full list on hover. A real sliced layer
usually mixes perimeter and infill speeds, so a single number would be a
lie; the count is highlighted when >1 because that's both normal
(perimeter vs infill) and exactly how a half-applied speed edit looks.

**Hover readout.** A proper bottom-of-screen status strip (fixed, no
decoration, `NoInputs` so it can never eat a viewport click) showing the
hovered path's object, path number, layer and speed. It reuses the exact
same `pickNearestPath()` call a click makes -- a separate "close enough
to hover" rule would eventually disagree with the click and be worse than
no readout. Resolved to plain values in `main.cpp` where the Scene is in
hand, rather than handing `EditorUI` a raw ref and making it do lookups.

**Travel editing.** Splitting and speed edits already worked on travels
(`applySpeedToPaths` only ever skipped PTP, correctly -- `$VEL.CP`
doesn't control point-to-point motion). The real gap was SELECTION: the
layer table is print-only by definition, since travels carry no layer, so
nothing offered a way to grab travels as a group. Added
`travelPathNumbers()`/`printPathNumbers()` and "Select travels" /
"Select prints" buttons. The speed panel now also states the print/travel
split of the current selection, plus a PTP-skip note -- "12 paths
selected" doesn't tell you whether you're about to change a print speed,
a travel speed, or both.

**Tab hides all panels**, edge-detected like undo/redo and guarded by
`WantCaptureKeyboard` so it can't fire while ImGui is using Tab to move
between text fields. The status bar deliberately stays visible when
panels are hidden -- the hover readout is most useful precisely then.

**Verified:** 202 tests (10 new covering travel selection, travel speed
application, and travel splitting); Debug and Release clean; the real
24,268-line file still round-trips `byteIdentical=yes`.

## Two UI bugs from real use: invisible mirror panel, phantom heightmap grid

**"The mirror option has no panel in the UI."** It was being drawn --
`drawMultiPartPanel()` was called every frame -- but as a
`CollapsingHeader` WITHOUT `DefaultOpen`, sandwiched between sections
(Transform, Layers) that do default open. A collapsed bar that looks
unlike every neighbour is effectively invisible. Added `DefaultOpen` and
a separator. Worth noting as a category: "it renders" and "it can be
found" are different claims, and only the second one matters.

**"5x5 heightmap but I see 12 lines in the grid."** The values were
right and the mesh was right -- what was wrong was that the heightmap had
no lines OF ITS OWN. A flat surface with every elevation still 0 sits
exactly coplanar with the bed reference grid, so the bed's 100mm lines
(1000mm bed / 100mm spacing = 11 lines) showed through and read as the
heightmap's own divisions. Fixed by drawing the heightmap's actual cell
boundaries: full row and column lines only, deliberately NOT via
`glPolygonMode(GL_LINE)` on the mesh, which would also draw every
triangulation diagonal and make a 5x5 grid look like a grid of triangles.
Lifted 0.5mm in Z so it can't z-fight with the bed grid in the
all-zeros case that caused the confusion in the first place.

## The safe point is a CELL property, not a part property

The operator read the real position off the pendant:
**X 970.7, Y 1760.8, Z 1005.0**.

That immediately exposed how wrong the derived anchor was. GcodeForge had
been drawing the marker at the program's first Cartesian point --
X 291.12, Y 2027.09, **Z 4.20** -- which is a full METRE too low and
~680mm off in X. The anchor sat on the bed; the actual safe pose is up in
the air, which is what a safe pose is *for*. The header comment on
`StartPoint::position` always said "display anchor, not derived truth,"
and this is exactly the gap that warning was about -- but a warning in a
comment doesn't help the operator looking at a crosshair in the wrong
place.

**The architectural insight, from the operator's own framing:** the same
robot goes to the same safe pose for every job. It's a property of the
CELL, not of the part. So it belongs on `BedSettings` (saved and loaded
with the bed file), not on `SceneObject` -- entered once per machine
rather than re-read off the pendant for every file.

- `BedSettings` gains `safePointMeasured` + `safePointX/Y/ZMm`, persisted
  by `io/BedIO` alongside size, origin, grid and heightmap.
- `StartPointRenderer::rebuild()` now takes the bed: a measured point is
  drawn ONCE in world space and does NOT ride any object's transform
  (the robot doesn't move its safe pose because a part moved). With a
  measured point present, the per-object derived anchors are suppressed
  entirely -- they were only ever standing in for this.
- Bed panel gains a "Robot safe point" section with the pendant workflow
  written into the UI, and honest state labelling: green "Measured --
  marker shows the real position" versus amber "Not measured -- marker
  falls back to the program's first point, which is NOT the safe pose."
  The previous silent fallback looked authoritative while being a metre
  wrong.

**A test bug worth recording, because the failure mode is subtle.** The
first round-trip test compared the loaded `float` fields against `double`
literals (`970.7`) at `checkNear`'s 1e-6 tolerance. `float(970.7)` and
`double(970.7)` differ by ~1.2e-5, so X and Y failed while Z passed --
1005.0 being exactly representable in float. Nothing was wrong with the
save/load. Fixed by comparing loaded-against-original rather than
loaded-against-literal, which is the assertion that actually carries
meaning ("what I saved is what I got back") and is immune to float
representation entirely.

**Verified:** 211 tests; Debug and Release clean; the real 24,268-line
file still round-trips `byteIdentical=yes`.

## "Mirror doesn't work" -- it did, but as the wrong shape of feature

Investigated against the REAL 24,268-line file rather than the synthetic
sample, by adding a mirror+interleave check to the `GCODEFORGE_TEST_FILE`
diagnostic. The underlying math was correct all along:

    mirror check: source world X [13.2 .. 562.9],
                  mirror world X [762.9 .. 1312.6], gap=200.0mm (clear)
    interleave check: 452ms, 48127 paths, 98 layers   <- 49 x 2, exact

So nothing was broken numerically. What was wrong was the **shape of the
feature**: it was split across two buttons ("Mirror the object", then
"Build interleaved print"). Mirroring alone produces a scene that looks
like nothing useful happened, and the second button reads as optional.
The operator's own description of what they wanted -- "when mirroring you
should make a new object and links layer by layer until done" -- is one
action, not two. Merged into a single **"Mirror and link layer by
layer"** button backed by a new `mirrorAndInterleave()` in
`editor/InterleavePrint`. Nobody wanted mirroring *without* the linking
here, so offering it separately only created a way to get a useless
half-result and conclude the feature was broken.

Also fixed while in there:
- **`Scene::addObject` invalidates `SceneObject*`.** The old multi-copy
  loop held a pointer across `addObject` calls, which `push_back` can
  invalidate by reallocating the vector. It happened not to crash given
  the exact ordering, but it was one edit away from a use-after-free.
  `mirrorAndInterleave()` re-looks-up by id every iteration instead.
- **Layer actions were silently dropped by interleaving** -- confirmed by
  the same diagnostic (`layerActions carried = 0`). A part-cooling
  command set up before mirroring would simply vanish from the merged
  program. Given that a comment-only cooling preset had *already* cost
  one real print, a second silent path to "cooling doesn't happen" was
  not acceptable. `buildInterleavedObject()` now records a
  `(source object, source layer) -> merged layer` map as it emits, and
  re-attaches each object's actions to the merged layer its segment
  actually became. Each part keeps its own copy, so with 3 mirrored
  parts a per-layer cooling command fires for each part's own layer --
  which is what per-layer cooling means once interleaved.
- The button now writes a result line ("Built: <name>" or the reason it
  failed), because the merged object appears at the bottom of the object
  list, off-screen, and a button that appears to do nothing is
  indistinguishable from one that is broken.

**Verified:** 221 tests (10 new, including cooling surviving the merge
end-to-end into the exported file); Debug and Release clean; the real
file still round-trips `byteIdentical=yes`.

## Print order was invisible; project files (milestone 12)

**"The mirroring is wrong, it should go layer by layer not finish one
object then do the other."** Verified against the real file by dumping
the actual emission order:

    interleave order: first 12 segments = ABABABABABAB
                      (longest same-part run: A=1 B=1)
    interleave order: ALTERNATING correctly

So the program was already correct -- it never prints two segments of the
same part consecutively. The problem was that **print order is invisible
in a static render**: an interleaved multi-part program and a sequential
one produce identical finished geometry. Both parts end up fully built
either way, so there was no way to tell them apart by looking, and the
reasonable conclusion was that it hadn't worked.

Fixed by adding **`ColorMode::Sequence`** -- colours each path by where it
falls in the program, blue (first) through red (last), on a continuous
5-stop ramp rather than the 18-colour palette (which wraps every 18
entries and would destroy the ordering signal on a 24k-path file). Under
this mode the difference is unmistakable: interleaved parts each show the
FULL blue-to-red gradient, sequential parts show one cool block and one
warm block. The feature was right; the ability to trust it was missing.

Lesson worth keeping: when a correct feature is reported as broken, the
bug may be in the *observability*, not the logic. Adding the check to the
diagnostic first -- rather than "fixing" working code -- is what kept
this from becoming a regression.

**Project files, `io/ProjectIO` (closes milestone 12).** A `.src` is a
robot program: it holds what the robot needs and nothing else. Everything
the EDITOR knows has nowhere to live in one -- selections, selection
group names/colours, per-object colours and visibility, transforms, the
bed and its measured heightmap, the measured safe point, pending object
links, display filters, colour mode, lighting. All of it died with the
app, and re-importing the SRC couldn't recover it because the
information was never in the file to begin with.

Format is plain text, one record per line, `OBJECT`/`ENDOBJECT`
delimited -- deliberately not JSON: no dependency, diffable in git, and
hand-repairable if a session file is ever half-written. Source lines are
stored verbatim, because `SrcExporter` patches *those* and its whole
preservation guarantee (E1-E6, `C_VEL`, interrupt logic, comments) rests
on them being byte-exact. `loadProject()` builds into locals and only
commits on success, so a corrupt file can't half-destroy an open
session; loading also resets the undo stack, since that history refers
to the previous scene and undo would otherwise restore a foreign one.

**A bug my own earlier fix caused.** `BedHeightmap`'s default
initializer pre-sizes `elevationsMm` to `cols*rows` zeros -- that was the
fix for an out-of-bounds crash. In the project loader it worked against
me: `heightmapValue` records are `push_back`'d, so they appended to the
50 pre-existing zeros, the count no longer matched `cols*rows`, and the
consistency check replaced everything with zeros -- silently discarding
every measurement in the file. Caught by the round-trip test asserting a
specific value (3.25 at a specific cell) rather than just checking the
grid dimensions. Fixed by clearing before reading.

**Verified:** 252 tests (11 new, each asserting a specific thing a `.src`
cannot hold); Debug and Release clean; the real file still round-trips
`byteIdentical=yes` and still interleaves ABABAB.

## Flat cross-part travels, and vertex display

**"The travels go up, move forward, then go down. I don't want any
movement in Z when moving."** The operator was right, and the lift was
over-engineering on my part.

I had built the cross-part transition as up-to-clearance, across,
down -- reasoning about collisions in the abstract. What that missed is
the defining property of interleaving: **every part is printed to the
SAME layer height simultaneously.** At the moment of any cross-part move,
every part is exactly as tall as the nozzle is high, so a straight
horizontal move passes through the empty gap BETWEEN parts and never over
material. The lift bought nothing and cost travel time plus another
chance to string.

Replaced with a direct horizontal move at the current layer height. The
one genuine hazard the operator also identified -- 3+ parts in a row,
where returning from the far part to the first would cut through the
middle one -- is handled the way they suggested: an intermediate
waypoint, but routed around in **Y**, still at constant Z. Whether a
detour is needed is decided per move by Liang-Barsky segment-vs-rectangle
clipping against each part's world XY footprint (the same technique the
marquee selection uses), so a direct move is only replaced when it would
genuinely clip something.

**Measuring the right thing.** The first diagnostic asked "is any travel
flat?" and reported `2.000mm (HAS Z MOTION!)` -- which looked like a
failure but wasn't. A layer-to-layer move MUST rise one layer height;
that's the print advancing, not a lift. On this file one layer is exactly
2.000mm, so 2.000mm was the floor, not a defect. Reworded to the question
that actually matters -- "does any travel rise by MORE than a single
layer step?" -- and the test asserts that bound rather than flatness.
Asserting the wrong invariant would have failed forever on correct
output.

**Vertex display** (`render/VertexRenderer`, "Vertices" in the Display
row, with a size control). Draws a point at every path endpoint. Off by
default -- on a 24k-path file all vertices at once is noise -- but it's
the only way to see where one motion command ends and the next begins: a
single long straight run and twenty short collinear moves look identical
until the vertices are visible. It's also how you confirm a path split
landed where you intended. Only each path's END point is emitted (plus
the very first FROM), since connected paths share vertices and emitting
both ends would double the buffer to draw every interior point twice.
Selected paths' vertices use the highlight colour.

**Verified:** 252 tests (the clearance-hop assertion replaces the old
safe-height one); Debug and Release clean; on the real file:
`largest travel Z change = 2.000mm (one layer = 2.000mm) (no clearance
hop -- only the layer step itself)`, still `ABABABABABAB`, still
`byteIdentical=yes`.

## Export writes the moved position now; interleaved speed was silently 0

Two real bugs from real use, both in the exporter's core logic.

**"After I moved paths, the exporter kept the old positions."** Root
cause was a genuinely wrong comparison in the coordinate-patch loop:

    glm::dvec3 exportPos = applyTransform(object.transform, path.to);
    glm::dvec3 originalPos = path.to;
    bool changed = glm::length(exportPos - originalPos) > 1e-6;

That asks "does the object's TRANSFORM move this point?", not "did this
point CHANGE versus the file?". With an identity transform -- true for
any freshly-loaded file, and true for the merged interleaved object --
`exportPos` and `path.to` are equal by definition regardless of what the
operator did to `path.to` directly (gizmo drag, connected drag, bed
conform, a moved-and-nudged selection after mirroring). Every edit
applied straight to a path's coordinates, with no accompanying transform
change, silently failed to export. It only ever worked in testing because
every existing test happened to set a transform first.

Fixed by comparing against what the SOURCE LINE currently says instead of
against the model: `editor/KrlLineEdit` gained `readKrlAxisValue()`, and
the patch loop now asks "does the file's own X/Y/Z differ from what we
want to write?" -- which is the only question that's actually meaningful,
since the model has no memory of the file's original value once a
coordinate has been edited in place. Confirmed with a test that moves a
path directly under an identity transform and re-parses the export to
check the MOVED value survived, not the original. Cost: export on the
24k-path file went from ~3ms to ~195ms (three regex reads per path
instead of trusting the model) -- worth it for correctness on a save
that's triggered by hand, not in a hot loop.

**"After export, the speed is 0."** Confirmed on the real file: the
merged interleaved program contained ZERO `$VEL.CP` commands anywhere.
Root cause: `buildInterleavedObject()` gives every path a genuinely real
(non-synthetic) `srcLine`, which made `SrcExporter`'s two-timeline speed
logic assume the file's own line already asserts the correct speed "for
free" -- a valid assumption when PATCHING a real file (that's the whole
design), but false for a program built from nothing that never had any
`$VEL.CP` in it until something writes one. The robot received no speed
command at all and stayed at whatever it was left at.

Fixed at the source: `emit()` (the helper `InterleavePrint` uses to
append every motion line) now writes a real `$VEL.CP` command into the
generated source whenever the required speed changes, exactly mirroring
what a real Eidos file does and what `SrcExporter` expects to find
already there. PTP motion is skipped, matching `SrcExporter`'s own rule.
On the real file: 195 `$VEL.CP` commands in the exported program, zero of
them zero.

Also added: `SpeedColorTable`'s gradient is now continuous (red -> green
at a fixed 0.6 pivot -> blue) instead of a discrete palette lookup,
matching the bed heightmap's gradient convention so "which speed is which
color" reads the same way in both panels. The pivot is fixed at 0.6
deliberately (not the data's own midpoint) -- a file that never drops
below 0.6 should read all-green-to-blue, because it genuinely never ran
slow.

**Editor panel reorganized into three tabs** (View / Scene / Object:
<name>) instead of one long scroll of ten stacked sections -- which is
how "Mirror the object" ended up invisible in the first place, as just
another collapsed bar buried in the middle. The Object tab is labelled
with the active object's own name, since with several mirrored copies in
the list "Object" alone stops being enough to know which one you're
editing.

**Verified:** 270 tests (6 new: the direct-edit export bug, the
interleave-speed bug end-to-end into exported text, and the speed
gradient's pivot/edge cases); Debug and Release clean; the real file: 195
non-zero `$VEL.CP` commands, still `ABABABABABAB`, still `byteIdentical=yes`.

## Rotate: a button and an interactive gizmo

**"I want the option to rotate the paths."** Two ways in:

**`editor/RotatePaths.h/.cpp`** -- a "Rotate selected" button (Object tab,
next to Split) that spins every selected path some entered angle around
the SELECTION's own centroid (not the object's pivot -- "rotate this
group in place" is what "rotate the paths" means, not "swing them around
wherever the object's origin happens to be"). Z-axis only, matching
`Transform::rotZDegrees`' own convention and every other rotation in this
app -- robotic print objects sit on a flat bed and spin around the
vertical axis, they don't tumble. A/B/C tool orientation is left
untouched, same as the existing whole-object transform (re-deriving
orientation from a rotated position would produce an unverified pose).

**A real interactive gizmo**, per the follow-up request ("make a gizmo
for it... size relative to camera distance... always half size"):

- **R** toggles the gizmo between Move (the existing arrows) and Rotate
  (a single ring). Deliberately one ring, not a 3ds-Max-style 3-axis
  ball -- a second or third rotation axis would just be two controls that
  do nothing in an app whose whole rotation model is Z-only.
- **Constant screen size**, not constant world size. `Camera::
  gizmoWorldRadius()` computes the world-space length that currently maps
  to a fixed fraction of the viewport height -- distance*tan(halfFovY) in
  perspective (the standard "world units per screen height at this depth"
  relation), the current ortho half-extent in orthographic. A fixed world
  size would shrink to invisible zoomed out, or dwarf the model zoomed in
  close; this is what "always half size" asked for. Picking geometry is
  built from the SAME formula the renderer uses (previously the move
  gizmo's own pick test used a hardcoded `kAxisLengthMm` while the
  request was already pushing toward a dynamic size -- fixed together, or
  clicking exactly on the visible gizmo could miss it once size started
  varying).
- Dragging the ring measures the on-screen angle around the gizmo's
  screen-space center (`angleAroundScreenPoint`, with the Y flip that
  makes clockwise-on-screen a NEGATIVE angle -- matching
  `rotZDegrees`' documented counterclockwise-positive convention, so
  a drag rotation and a typed angle turn the same way) and applies the
  accumulated delta from a FIXED start snapshot every frame, not
  incrementally -- the same reasoning `closestPointOnAxisToRay`'s doc
  comment already gives for the move gizmo: a moving reference basis
  makes frame-to-frame deltas meaningless.
- **Object mode** pivots the whole object's `Transform` around the
  gizmo's own origin (the geometry centroid, not the raw `x/y/z` pivot)
  via a closed-form `rotateObjectAroundPivot()`: composing an existing
  transform with an additional world rotation about an arbitrary point
  only needs adding to `rotZDegrees` and re-deriving the translation that
  keeps the pivot fixed -- one `rotatePointAroundPivotZ()` call, not an
  iterative solve.
- **Start/End/Whole modes** reuse `buildDragSnapshots()` -- the exact
  same connectivity-aware snapshot the move gizmo already uses -- so a
  connected unselected neighbor's touching endpoint rotates around the
  SAME pivot too, staying attached under rotation exactly like it stays
  attached under translation.

**Verified:** 283 tests (13 new: `rotateSelectedPaths()`'s centroid math
and no-op-on-empty-selection, plus the pivot-rotation primitives'
fixed-point and object/point-agreement properties -- one of these caught
my own test-authoring mistake, a from==to path whose "centroid" was
trivially itself and couldn't move under any rotation, exactly the kind
of self-cancelling test worth catching before it hides a real bug behind
a false pass); Debug and Release clean; real file still round-trips
`byteIdentical=yes`.

## Interleave travels missing A/B/C/E1-E6 (rejected file + failed pendant load)

Real-use report, with photos: a 4-copy mirror+interleave export of the
production file failed on two fronts at once. The web GCode Editor's own
structural validator popped up "Output has 1630 critical validation
issue(s). Download for offline review anyway?" And separately, the KUKA
pendant's OrangeApps PointLoader plugin refused the same file with
"PointInfoCollection is null." (The pendant plugin is proprietary --
there's no public documentation of its internal error codes to consult,
so this was root-caused from the file format itself rather than from
pendant-side docs.)

**Root cause**, found by reading the web editor's own validator source
(`validateLines` in the original app, still present in
`_learn/-Gcode-Editor/index.html`): every real motion line in this format
carries 12 fields always emitted together --
`{X,Y,Z,A,B,C,E1,E2,E3,E4,E5,E6}` -- confirmed against the real file
(`LIN {X 291.12, Y 2027.09, Z 4.20, A 164.577, B 90.000, C 164.767, E1
0.000, ... E6 0.000 } C_VEL`). `InterleavePrint.cpp`'s synthetic
cross-part travel and in-layer-reposition lines used a hardcoded
`"LIN {X 0,Y 0,Z 0}"` stub -- X/Y/Z only, missing tool orientation
(A/B/C) and all six extruder axes (E1-E6) entirely. The web validator
flags every incomplete LIN line as CRITICAL (9 missing fields each); a
4-copy interleave of the real file generates 585 synthetic lines, close
enough to the reported 1630 (9 × ~181 of them, the rest being print
lines that DO carry their real template) to confirm this is the same
bug. The pendant's point loader almost certainly rejects the same
structurally incomplete lines for the same reason -- a motion point
missing orientation/axis data isn't a valid point to load.

Two copies (the object count every earlier interleave check used) never
exercised the Y-detour code path at all -- that only fires going from
the far part in a row back past a middle one, which needs 3+ copies. The
in-layer-reposition stub was hit by both counts, but at a lower volume,
which is likely why this had not surfaced until the user specifically
tried 4 copies.

**Fix:** `InterleavePrint.cpp` now tracks `lastFullTemplateLine`, updated
to the most recent REAL motion line's template every time a genuine
(non-stub) axis line is emitted (detected via `hasAxisFieldA`, since a
line either carries the whole A/B/C/E1-E6 set together or none of it).
Both synthetic call sites now build from `syntheticTemplate()`, which
takes that real template, strips any trailing `C_VEL` (travels should
stop precisely at each waypoint rather than blend through the gap
between parts -- matching the original stub's intent), and appends the
identifying comment. `replaceKrlAxisValue` then re-points X/Y/Z at the
actual travel target exactly as before; A/B/C/E1-E6 now come along for
free from the borrowed template instead of being dropped.

New test `testInterleaveTravelsKeepFullAxisSet()`: builds a 3-copy
mirror+interleave from a small fixture where every LIN line carries the
full 12-field set (unlike the older lightweight fixture, which only puts
A/B/C on one line -- not representative of a real robot file), forces
the Y-detour by using 3 copies, and checks every exported line
containing the `GCODEFORGE INTERLEAVE TRAVEL` / `GCODEFORGE in-layer
reposition` markers has all 9 of A/B/C/E1-E6 present.

**Verified:** 285 tests, Debug and Release clean. Real file
(`GCODEFORGE_TEST_FILE`), 4-copy interleave (the exact scenario
reported): 585 synthetic travel/reposition lines, 0 missing
A/B/C/E1-E6 (was the bug before the fix). Round-trip export of the
untouched file still `byteIdentical=yes`.

## Interleaved export discarding the whole safety header and shutdown footer

Real-use report, with photos comparing a 1-up original file against the
same part mirrored 5 times and interleaved: the 5-up file would not load
on the robot at all. The screenshots (Print-order color mode) showed the
1-up object with a distinct starting travel (safe-pose PTP + initial
travel down to the print) and ending travel (final retreat + shutdown),
both visible as colored paths -- and the 5-up object had neither.

**Root cause:** `buildInterleavedObject()` never copied any of the
original program's own structure. It started every merged program from a
bare `DEF GCODEFORGE_INTERLEAVED()` and ended with a bare `END`, discarding
`&ACCESS`, every safety interrupt declaration, `BAS(#INITMOV,0)`, the
joint-space safe-pose PTP, the initial travel down to the print's start
point, and -- critically -- the entire shutdown block at the end
(`$OUT[...]=FALSE` for air/extruder/bed-heat, `$TIMER_STOP`). A program
missing `&ACCESS` and its safety interrupts is exactly the kind of thing
a real controller refuses to load, and even if it somehow had loaded, it
would have finished printing without ever turning off the extruder or bed
heat. The user explained the retreat travel's actual purpose: it moves
the head away from the object so it doesn't drip on the finished part --
confirming it needs to be transformed per-part, not treated as some fixed
global parking position.

**Fix:** `buildInterleavedObject()` now finds each object's own real
Cartesian path span (`pathSrcLineSpan()`) and lifts everything before the
FIRST object's first path (the header) and everything after the LAST
object's last path (the footer) verbatim via a new `extractBoilerplate()`
helper -- coordinate-patching any embedded LIN/PTP lines through that
object's own transform (so the header, from the un-mirrored original,
comes through unchanged, and the footer's retreat travel, from whichever
copy printed last, is correctly re-transformed to retreat from THAT
copy's own position, exactly matching the drip-avoidance purpose the user
described). The header's paths and the source object's `startPoint`
(joint-space safe pose) are registered as real entries on the merged
object -- not just background text -- so they render, select, and color
in the viewport exactly like the screenshots show for a normal file.
Path numbering runs header paths first, then the interleaved body, then
footer paths, continuing the same sequence throughout.

Also added, same real-use report: an FPS counter in the status bar
(bottom-right corner), reading ImGui's own internally-smoothed
`io.Framerate` -- no separate timer needed.

New test `testInterleavePreservesHeaderAndFooter()`: a fixture shaped
like a real Eidos program (`&ACCESS`, interrupt declarations, safe-pose
PTP, print body, shutdown footer), mirrored 5 times and interleaved,
checking the export still contains `&ACCESS`, the interrupt declaration,
the safe-pose PTP, all three shutdown `$OUT[...]=FALSE` lines, and a real
trailing `END` -- and that re-parsing the export recovers exactly the
same path count and startPoint the model reports (proving header/footer
travels are tracked paths, not just preserved text).

**Verified:** 299 tests, Debug and Release clean. Against the user's own
real 1-up file (`E:\CELL01-HUB_SMALL_B2B_REV_B-1UP-.src`), mirrored 5
times (the exact scenario reported): exported program has `&ACCESS`,
`INTERRUPT`, the safe-pose PTP, all three shutdown outputs, ends with a
real `END`, `startPoint.present = true`, and re-parsing the export
recovers all 33,227 paths the model reports (exact match). Round-trip
export of the untouched original file is still `byteIdentical=yes`.

## Cell template: capture + check + fix for objects with no boilerplate of their own

Forward-looking question after the header/footer preservation fix: "next
I want to import sliced objects and they will not have start and end, and
you have to fix it with a button or a check." A plain sliced .gcode/.nc
import (parser/GcodeParser.h) has no &ACCESS, no safety interrupts, no
safe-pose PTP, and no shutdown block at all -- the header/footer fix only
works when SOME object being merged/exported already has boilerplate to
lift, so a set of objects that all lack one would still fall back to a
bare "DEF .../END", reproducing the exact bug that fix solved.

**A real bug found while building this**: writing the test surfaced that
the header/footer boundary (`pathSrcLineSpan`) was computed over paths of
ANY type, including the object's own approach/retreat TRAVEL paths --
which put the boundary AFTER the retreat travel too, silently excluding
its actual motion (the lift + step-back that keeps the nozzle from
dripping on the finished part) from "footer boilerplate." Only the
shutdown $OUT/END *text* survived; the travel that's supposed to happen
before it did not. This affected the interleave fix already shipped, not
just the new cell-template feature -- confirmed and fixed by redefining
the span as PRINT-type paths only (`PathType::Print`), so travel paths on
either side of the print body -- both the interleave's own header/footer
lift and this feature's cell-template application -- are correctly
included. Caught by a new positional check (retreat's lift line must
appear, and must appear BEFORE the shutdown outputs) added to both the
test suite and the main.cpp real-file diagnostic; confirmed on the real
1-up file mirrored 5 times: `Z 508.00` (the real retreat lift) now
appears at line 34684, `$OUT[5]=FALSE` at line 34689 -- lift before
shutdown, as it must be.

**Design**: `editor/Boilerplate.h` is now a shared module (`pathSrcLineSpan`,
`extractBoilerplate`) used by both `editor/InterleavePrint.cpp` (unchanged
behavior, just refactored to share code) and the new `editor/CellTemplate.h`.
`CellTemplate` (header lines + footer lines) is a new field on
`BedSettings` -- cell-level, not object-level, matching the safe point:
the safety interrupts and I/O indices are properties of the robot cell,
not any one file. `objectHasBoilerplate()` is the "check" (true only if
BOTH a header and a footer exist). `captureCellTemplate()` lifts an
object's own header/footer (world-space, via editor/Boilerplate.h) into a
reusable template. `applyCellTemplate()` is the "fix": rigidly translates
the template's approach geometry so its own last coordinate-bearing line
lands exactly at the TARGET object's actual first print position, and the
retreat geometry so its first coordinate-bearing line lands exactly at
the target's actual last print position -- preserving the template's
relative shape (how far the approach descends, how far the retreat steps
back) regardless of where the new object sits on the bed. Non-coordinate
lines (safety declarations, the joint-space safe-pose PTP, $OUT/$TIMER
commands) are untouched, since `replaceKrlAxisValue` only acts where an
axis token is actually present. World-space template text is converted
back into the target object's own LOCAL space via `inverseApplyTransform`
before being stored as Path::from/to, since the target's own transform
still applies on top at render/export time.

**UI**: Bed panel gets a new "Cell template" section -- capture status,
"Capture from active object" (enabled only when the active object passes
`objectHasBoilerplate()`), and Clear; persisted with the rest of
`BedSettings` via `io/BedIO.cpp` (one line per header/footer source line,
since arbitrary KRL text with braces/commas can't go through the existing
generic "key value" numeric reader -- same special-casing as
`heightmapPoint`). The Object tab shows a warning panel (only when the
active object fails the check) explaining what's missing and why it
matters, with a "Fix using cell template" button when a template has been
captured, or a note to capture one first when it hasn't.

New tests: `testCellTemplateFixesSlicedImport()` -- captures a template
from a realistic fixture, applies it to a `parseGcode()`-origin object
(genuinely no boilerplate at all, the real target scenario) far away on
the bed, and checks the check correctly flags it before the fix and
clears after, the approach/retreat anchor to the sliced object's OWN
actual print start/end (not the template's original position), and the
export has &ACCESS/shutdown/END. `testInterleaveTravelsKeepFullAxisSet`
and `testInterleavePreservesHeaderAndFooter` extended with the
lift-before-shutdown positional check described above.

**Verified**: 313 tests, Debug and Release clean. Real 1-up file
(`E:\CELL01-HUB_SMALL_B2B_REV_B-1UP-.src`) mirrored 5 times: &ACCESS,
INTERRUPT, safe-pose PTP, all three shutdown outputs, real END,
startPoint present, matching re-parsed path count (33,232), retreat lift
present and correctly ordered before shutdown. Round-trip export of the
untouched file still byte-identical. (Note: the same 5-copy check hangs
under a Debug build specifically, taking minutes instead of seconds on
this ~14-25k-line real file -- confirmed via Release, which completes in
under a second with identical, correct results, that this is Debug's
iterator-checked container overhead on repeated large-scale diagnostics,
not a logic bug; the parser_smoke_test suite's smaller fixtures pass
quickly in both configs.)

## Pre-export validation report (structural + speed reparse), from Codex's read of the web original

Asked Codex (working from the web Gcode Editor's real source,
`_learn/-Gcode-Editor/index.html` + `export-verification-v481.js`) to
write up everything non-obvious about its export logic, specifically so
gaps in GcodeForge could be found and fixed against the original's actual
behavior rather than guesswork. Most of what came back already matched
(path recognition, travel-state-via-comments including the implicit-
initial-travel rule, literal Z-delta layer detection, the two-timeline
speed model, PTP excluded from `$VEL.CP`, first-object-header/last-
object-footer for combined exports, transforms only touching XYZ). The
one real gap: GcodeForge had no pre-export validation at all -- no
structural check, no speed-value verification, nothing standing between
a broken export and the file dialog.

Added `editor/ExportValidation.h/.cpp`, matching the web validator's
exact rules:
- **Structural (CRITICAL, blocks export)**: exactly one `&ACCESS`,
  exactly one `DEF`, exactly one standalone `END`, no recognized motion
  after that `END`, and every `LIN` target line must carry the full
  `X Y Z A B C E1 E2 E3 E4 E5 E6` set. That last rule is the SAME
  completeness check that would have caught, automatically and before
  ever reaching the user, the exact bug reported earlier this session --
  a synthetic interleave travel line shaped like `"LIN {X 0,Y 0,Z 0}"`
  with no A/B/C/E1-E6 at all, which the web editor's own validator had
  flagged with 1630 CRITICAL issues. A regression test reproduces that
  exact line shape and confirms it's caught.
- **Travel markers (WARNING)**: an unmatched `;travel end` is flagged,
  except the one legitimate implicit-initial-travel case (a real Eidos
  file may begin already inside a travel section) and ending the program
  while still in travel state (the real shutdown sequence does this) --
  ported the pre-scan technique `parser/SrcParser.cpp` already uses to
  establish initial travel state, after an earlier version's inline
  "have I used my one free pass" bookkeeping let a SECOND unmatched
  `;travel end` slip through uncaught (caught by a repeated-marker test).
- **Speed verification (structural mismatch CRITICAL, value mismatch
  WARNING)**: reparses the compiled text as a fresh object via the real
  `parseSrc()` (not comparing internal bookkeeping) and checks every
  non-PTP path's actual speed against what the object intended, at 1e-9
  tolerance, capped at 12 reported mismatches -- matching
  export-verification-v481.js's behavior exactly: a PATH COUNT mismatch
  stays a hard failure (something structural broke), but a pure
  speed-VALUE mismatch downgrades to a warning rather than blocking.

UI: "Save SRC As..." now compiles the object first and runs the full
report before ever opening the file dialog. A clean report (the common
case) skips the modal entirely. Otherwise a popup lists every issue,
color-coded by severity, with "Save Anyway" available only when nothing
is CRITICAL (matching the web app's `SRC EXPORT BLOCKED` behavior for
structural failures having no override).

**Verified**: 332 tests, Debug and Release clean. Real 1-up file: the
untouched round-trip export reports 0 critical/0 warning (confirming the
validator doesn't false-positive on a genuinely correct file -- a
validator that does just trains the operator to click through it). The
5-copy mirror+interleave export (the exact scenario that used to fail to
load on the robot) ALSO reports 0 critical/0 warning now, end to end.

## Print animation: real-time simulation with progressive geometry reveal

Requested after discussing whether to just port more web-editor features
wholesale (decided not to -- targeted, verifiable asks work better than
a blanket dump). The web Gcode Editor DOES already have an Animation
Module (checked directly: `_learn/-Gcode-Editor/index.html` around
`buildAnimationSequence`/`stepAnimation`/`drawPrintHead`), but reading it
revealed it isn't what was actually wanted: it drives a moving marker dot
by DISTANCE over geometry that's already fully, permanently drawn -- no
time readout, no progressive reveal. A single long straight LIN move
would have the dot crawl across a bead that was visible the whole time,
which is exactly the "can't simulate realistic printing" gap raised.
Built deliberately more than a port:

**Core model** (`editor/PrintAnimation.h/.cpp`, GL-free and fully unit
tested): every path is split into equal-length sub-segments no longer
than a configurable limit (default 5mm) so a long straight move reveals
gradually instead of popping in whole. Each segment carries its own
cumulative distance AND cumulative TIME (`length / effectiveSpeed()`,
mm/s), giving a total print-time estimate for the whole sequence. The
single shared primitive, `stateAtTime(sequence, timeSeconds)`, is called
both by real-time playback (stepped by wall-clock delta * speed
multiplier) and by timeline scrubbing (jumping straight to a time value)
-- same function either way, so play and scrub can never show a
different head position for the same instant. A path with no recorded
speed falls back to a configurable speed rather than stalling the whole
simulation at that one path.

**Reveal rendering** (`render/AnimationRenderer.h/.cpp` +
`render/AnimationShader.h/.cpp`): the real engineering question was how
to reveal geometry on a 20k+ segment real file without a per-frame CPU
mesh rebuild. Each segment becomes a small independent box (no mitred
joints between neighbors -- an acceptable seam for a preview, and far
simpler than GeometryRenderer's mitred-run algorithm, which is built for
a different job); every vertex carries the distance at which its own
segment starts printing. The mesh uploads ONCE when the simulation is
built; every frame afterward just updates a single `uRevealDistanceMm`
uniform and the fragment shader discards anything not printed yet.
Confirmed on the real file: 20,309 segments, 162,472 triangles, zero GL
errors building the mesh or drawing at the start/middle/end of a
2648-second (~44 minute) simulated print.

**Print head** (`render/PrintHeadRenderer.h/.cpp`): a simple two-box
marker (head body + nozzle) whose nozzle tip sits exactly at the current
playback position, reusing the existing MeshShader/MeshVertex pipeline
rather than a new shader -- rebuilt fresh every frame, which is cheap
regardless of file size since it's always just two boxes. Deliberately
axis-aligned rather than matched to the robot's real tool orientation
(Path::a/b/c) -- noted as a follow-up, not required for the simulation to
read correctly.

**UI**: new "Animate" tab -- scope filters (include print/travel),
subdivision length, fallback speed, print-head dimensions, Play/Pause/
Stop, a scrub timeline, and elapsed/total time + percentage. While a
simulation is built, it replaces the normal always-visible scene geometry
entirely (showing both at once would defeat the reveal) -- a known
simplification hides every object, not just the one being animated, an
acceptable rough edge for the common single-object workflow.

**Verified**: 374 tests (42 new), Debug and Release clean. Real file GL
smoke test (parser_smoke_test can't create a GL context, so this runs
inside the `GCODEFORGE_TEST_FILE` diagnostic instead, the only way to
catch a GL-side bug before it ships): build + draw at three timeline
points, zero `glGetError()` at every step.

## Mirroring was actually a safety bug: copy, don't flip

Real-use report, found via the new print animation: after mirroring and
interleaving, the animation showed the nozzle traveling FAST across an
already-printed line right after entering a mirrored copy -- the entry
travel was landing on the FAR edge of the copy instead of the near edge
facing the previous part. The user's own diagnosis was exactly right: "I
should have not called it mirroring, it's just copying" -- flipping the
geometry was never actually needed for the "print several copies,
interleaved so each cools" workflow, and it was actively dangerous.

**Root cause**: `mirrorObject()` flipped the copy's local X. Flipping
doesn't reorder the path list -- the print still starts at the same
FILE-ORDER point it always did -- but flipping the geometry can relocate
THAT point to a different SIDE of the copy's own bounding box, with no
relationship to which side actually faces the neighboring part.
`InterleavePrint.cpp`'s `emitTransition()` targets a layer's first point
directly and deliberately never checks whether the straight line to it
crosses the TARGET's own footprint -- it can't, structurally: the target
point is always ON that footprint's boundary by definition, so the check
is written to skip the target object entirely (`if (i == targetObjectIndex)
continue;`). If flipping put that first point on the far edge instead of
the near one, the "direct" transition would drag the nozzle across the
copy's own just-deposited material at full travel speed to reach it.

**Fix**: `mirrorObject()` is now a plain translated copy -- no flip. Kept
the function name (avoiding an invasive rename across every caller and
UI string) but updated every doc comment and the user-facing "Mirror the
object" panel, "Mirror and link layer by layer" button, and copy naming
(now "(copy)" not "(mirror)") to say what it actually does. The placement
math also needed fixing: the old formula (`2*maxLocalX + safeDistanceMm`)
was specifically derived for the flip case (mirrored range becomes
`[-maxX, -minX]`, so only the far edge matters); the plain-translation
case needs the object's actual WIDTH (`maxLocalX - minLocalX`), which
requires tracking `minLocalX` too (previously unused).

New test `testMirrorTransitionApproachesNearEdgeNotFarEdge()`: an
asymmetric single-layer fixture (local X range 20-100, so a flip
actually relocates something -- an earlier fixture attempt starting at
local X=0 didn't, since 0 negates to itself) with two copies, checking
the cross-part transition's actual landing point is closer to the
target's near edge than its far edge. Failed correctly against the old
flipping behavior (landed exactly on the far edge, confirming the exact
reported bug) before the fix, passes after. Existing `testMirrorObject()`
updated to assert flipX is now UNCHANGED, not toggled.

Also, unrelated but requested alongside this: the Animate tab's timeline
scrub bar now stretches to the full panel width and is noticeably taller
(it's the primary control in that tab, not a minor setting).

**Verified**: 377 tests, Debug and Release clean. Real file (4-copy and
5-copy interleave): all existing invariants (alternation, no clearance
hop, full axis set, header/footer preservation, retreat-before-shutdown,
zero validation issues, byte-identical untouched round-trip) still hold
with the corrected copy behavior.

## DXF spline import: 3ds Max splines become a layer-by-layer toolpath

Real request: the user modeled a rounded "picture frame" donut in 3ds Max
as a stack of splines, one per print layer, and wanted GcodeForge to read
them directly rather than requiring an Eidos slice first.

**Format, established from the real file**: DXF is a flat sequence of
(group code, value) line pairs. 3ds Max's own exporter emits far more
than the requested geometry -- light-gizmo BLOCK definitions, material
dictionaries, viewport/dimstyle tables -- so `DxfParser.cpp` only looks
inside the top-level `ENTITIES` section. Within it, the real file
contains 31 CLOSED 4-vertex `POLYLINE` rings at increasing Z (the actual
print layers, already pre-sliced, tracing a tapering rounded-rectangle
silhouette -- NOT identical stacked copies) plus several OPEN `POLYLINE`
entities spanning the full Z range that turned out to be 3ds Max's own
internal loft/rail construction curves, not layers at all. The DXF
"closed" flag (group code 70, bit 0) is what tells the two apart --
verified this distinction against the real pasted `ENTITIES` data before
writing any parsing code, since guessing wrong here would silently
"print" a construction curve.

**Units**: converted via the file's own `$INSUNITS` header value to
millimeters (GcodeForge's native unit). The real file uses centimeters
($INSUNITS=5, ×10); the resulting ~3.046mm layer spacing matched the
user's independently-stated "each spline is in 3mm increment" almost
exactly, confirming the parse was right before ever loading it into the
app.

**No implicit tool pose**: DXF is pure geometry -- no speed, no A/B/C
orientation. Both come from `DxfImportOptions` (asked the user directly:
"nozzle is straight down" -> `toolBDegrees = 90.0`), applied uniformly to
every synthesized line, rather than guessed -- a silently wrong tool pose
is a real safety issue on an actual robot, same principle as the existing
`coolingOutputIndex_` pattern.

**Speed and completeness**: same two-timeline mechanics as
`InterleavePrint.cpp` -- a synthesized object has no source `$VEL.CP` to
inherit "for free," so `emit()` writes an explicit `$VEL.CP` whenever the
required speed changes, and every `LIN` line carries the full
X/Y/Z/A/B/C/E1-E6 field set from the start (the exact completeness rule
`ExportValidation.h` enforces, and the exact bug class fixed earlier this
session for interleave travels).

**A design gap this surfaced in `CellTemplate.cpp`**: `objectHasBoilerplate()`'s
`hasHeader()` was pure line-count (`span.first > 0`), which would have
reported a DXF-imported object (which always has *some* line, its bare
synthetic `DEF` wrapper, before the first path) as "already has real
safety boilerplate" -- silently defeating the Cell Template fix for
exactly the objects it exists to protect. Fixed to require actual
`&ACCESS` content in the header and more than a bare `END` in the
footer.

**A real parser bug caught by its own test**: the very first ring's
first print path had `from == to` (both the ring's *second* vertex) --
the loop starts its print segments at the ring's second point, but
`cursor` (which supplies `path.from`) was never seeded with the ring's
actual first point before that. Harmless for the exported KRL text
itself, but wrong for `CellTemplate`'s header-anchor logic, which reads
`paths.front().from` as "the object's real print start." Fixed by
seeding `cursor` from the first ring's own first vertex before the print
loop runs, instead of only via an inter-layer travel (which only exists
from the second ring onward).

**Verified**: 15 new synthetic-fixture tests (ring/layer count, Z
ascending order + unit conversion, open-polyline exclusion, uniform tool
orientation, `$VEL.CP` insertion, full-field `LIN` lines, and that the
result still needs the Cell Template fix) plus a one-off end-to-end run
against the real `spline.dxf`: 31 layers, 154 paths, Z range 3.046mm to
94.418mm (~2.95mm average spacing), `objectHasBoilerplate` correctly
`false`. Wired into File > Open (`.dxf` extension dispatch) and the
native file dialog's filter. 392 tests total, Debug and Release clean.

## Hide layers, paths, selections, and groups (viewport-only)

Real request: "we forgot to add the most important part... hiding layers
and paths. Hide selected... for groups also." Object-level `visible`
already existed (`SceneObject::visible`, wired into `GeometryRenderer`),
but there was no way to hide anything finer-grained -- a single layer, an
arbitrary selection, or a saved selection group.

**Scope decision, confirmed with the user first**: does hiding something
also exclude it from the exported file, or is it purely a viewport aid?
Went with viewport-only, matching the existing `visible` flag's behavior
exactly -- one consistent rule for what "hide" means everywhere in the
app, rather than two different meanings depending on which hide button
was clicked. `SrcExporter` needed zero changes as a result; a dedicated
test (`testVisibilityDoesNotAffectExport`) confirms exported output is
byte-identical whether or not paths are hidden, hiding every print path
in the fixture as the extreme case.

**Design**: one new field, `SceneObject::hiddenPaths` (a `std::set<int>`
of path numbers, mirroring the existing `selectedPaths`), plus
`editor/Visibility.h/.cpp` with the actual operations. Layer-hide and
group-hide are NOT separate stored flags -- both just resolve to a set of
path numbers (reusing `pathNumbersForLayer()` from `editor/Selection.h`
for layers) and hide/show those same path numbers. One source of truth
avoids the layer table and a selection group ever disagreeing about
whether a path is hidden. `isLayerHidden()`/`isGroupHidden()` are
computed, not stored: a layer/group reads as hidden only when EVERY one
of its paths is currently hidden, so a partially-hidden layer correctly
shows as "not hidden" rather than lying with a checked box.

**Rendering**: `GeometryRenderer.cpp`'s main build loop now skips any
path whose number is in `hiddenPaths`, in both the geometry pass and the
selection-outline pass. A hidden print path also breaks a connected-run
boundary (same as a real position gap or a travel) so it can never
silently bridge two now-adjacent visible segments into one continuous
bead.

**UI**: a "Visible" checkbox column on the layer table (mirrors the
object list's existing one), "Hide selected" / "Show all" buttons next
to the existing selection tools, and a visibility checkbox per selection
group -- all following the established `undoStack.snapshotBeforeChange()`
+ `dirty = true` pattern the object-visible checkbox already used.

**Persistence**: `hiddenPaths` round-trips through `.gfproj` project
files (new `hidden <n>` line per path, ignored by older readers).

**Verified**: 23 new tests (hide/show/hide-selected/show-all, per-layer
hide with the partial-hide edge case, per-group hide, export
byte-identity, and project round-trip) plus every existing test still
passing. 415 tests total, Debug and Release clean.

## Speed override silently dropped by mirror/interleave

Real-use report, with a pre-export report screenshot: exported speed
0.02 for a run of layer-1 paths, "does not match the intended 0.06" --
the user had deliberately set layer 1 to a different speed than the rest
of the file (matching what an older reference tool showed as a distinct
color/speed for that layer), and the viewport never visually reflected
the change either.

**Root cause**: `buildInterleavedObject()` (`InterleavePrint.cpp`) reads
each source path's speed while re-emitting it into the merged object.
The call was `emit(..., path.speed)` -- the raw, ORIGINAL parsed speed
-- instead of `path.effectiveSpeed()`, which correctly prioritizes an
active `speedOverride`. Any speed override set on the source object
BEFORE mirroring/interleaving was silently discarded at exactly this
step: the merged object has no override concept of its own (`emit()`
bakes a real `$VEL.CP` + `path.speed` straight into the merged source
text), so once the override was dropped here it was gone for good -- the
interleaved copy reverted to whatever the file's own original speed was.

This explains BOTH reported symptoms with one root cause: the exported
text legitimately said 0.02 because the in-memory model's `path.speed`
itself was already 0.02 post-interleave (the override never made it into
the merged object at all) -- and the viewport wasn't failing to
re-render, it was accurately showing the (wrong) data that resulted from
the dropped override. `verifyCompiledSpeeds()` only compares the model
against its own export, so it couldn't catch a case where the model
itself already silently lost the user's intent before export ever ran.

**Fix**: `emit(path.from, path.to, PathType::Print, currentLayerNumber, templateLine, path.motion, path.effectiveSpeed())` at the print-path call site.

New test `testMirrorAndInterleavePreservesSpeedOverride()`: overrides
layer 1 to a speed distinct from the file's own (0.075 vs 0.040) on the
source object, mirrors+interleaves it, and checks the merged object's
layer 1 keeps 0.075 -- in the model, in the exported KRL text, and via
`verifyCompiledSpeeds()` reporting no mismatch. Confirmed this test fails
correctly against the pre-fix code (reproducing the exact "exported
speed does not match the intended" warning from the report) before the
fix, passes after.

**Verified**: 424 tests total, Debug and Release clean.

## Stuck in animation mode with no way back to the file

Real-use report: "when i do the animation and i want to go back to my
file i cant."

**Root cause**: a built print simulation (`animBuilt`, a `main.cpp`-local
bool) completely replaces the normal object viewport with the animation's
progressive-reveal mesh (`main.cpp:1367-1374`) -- by design, so the
already-fully-drawn object doesn't sit underneath defeating the reveal.
But nothing in the UI ever cleared `animBuilt` back to false: the "Stop"
button only paused playback and rewound time
(`main.cpp:962-966`), the View tab's Lines/Geometry render-mode buttons
have no effect while `animBuilt` is true (that branch is skipped
entirely), and switching tabs does nothing to it either. The only code
path that ever cleared it was an incidental side effect of making a
*different* object active than the one the simulation was built for
(`main.cpp:945-948`) -- unreachable in the common single-object case this
was reported from.

**Fix**: a new, explicitly-separate "Back to editor view" button in the
Animate tab (`EditorUI.cpp`'s `drawAnimationPanel`, enabled only once a
simulation is built), wired through a new `animationExitRequested_` flag
to `main.cpp` clearing `animBuilt` (and rewinding playback) -- kept
distinct from "Stop" specifically so pausing/stopping playback to scrub
around doesn't also destroy the built simulation.

**Verified**: builds clean, Debug and Release, 424 tests (no unit test
added -- this is UI/main-loop wiring with no ImGui/GL context in the test
binary, same as the pre-existing Stop button, which also has no unit
test; verified by reading the draw-dispatch code path directly).

## Export SRC dialog: individually-runnable checks + speed rounding

Real request: a menu for exporting with (1) an option to round exported
speeds to 4 decimals, (2) each pre-export check runnable one at a time to
see its own result, (3) a "run all" that produces the combined report,
then (4) save.

**Checks made individually runnable**: `validateStructure()`'s 6
structural checks (one `&ACCESS`, one `DEF`, one `END`, no motion after
`END`, complete LIN axis fields, balanced travel markers) and
`verifyCompiledSpeeds()`'s speed-match check were bundled into one pass
each. Split into 7 self-contained functions (`checkSingleAccess`,
`checkSingleDef`, ..., `checkSpeedMatch`), each under a uniform signature
`(object, compiledLines, speedToleranceMps, report)` so a UI can list and
run them individually -- `exportValidationChecks()` returns the named
list. `validateStructure()`/`verifyCompiledSpeeds()`/`validateForExport()`
are kept as thin wrappers calling the same functions in the same order,
so every existing caller and test is unaffected.

**Speed rounding**: new `SrcExporter::ExportOptions{ roundSpeedsTo4Decimals }`,
threaded through `buildExportedLines()`/`exportSrcToFile()`. Rounds
BEFORE the two-timeline redundant-insert comparison, not after -- two
paths differing only past the 4th decimal (0.06001 vs 0.05999) must be
recognized as the SAME written value (0.0600), or each would wrongly
think it needs its own redundant `$VEL.CP` line. `verifyCompiledSpeeds()`
gained a real `toleranceMps` parameter (default 1e-9, unchanged) because
rounding introduces an EXPECTED gap of up to 0.00005 between intended and
exported speed -- without widening the tolerance to match, every rounded
speed would falsely report as a mismatch.

**UI**: "Save SRC As..." now opens an "Export SRC" window instead of
silently validating and only popping up on an issue -- Options (rounding
checkbox), a Run button + live result per check, "Run all tests", a
combined report, then "Save SRC..." (blocked only while a RUN check shows
a critical issue; an unrun check doesn't block, but can't vouch for the
file either -- shown separately). The file write itself stays in
`main.cpp` (needs the native save dialog + `GLFWwindow*`, neither of
which `EditorUI` has); the dialog only decides whether and with what
options to save. Replaces the old reactive-only `ExportDecision`/
`showExportReport()` modal entirely -- no other code depended on it.

**Verified**: 9 new tests (rounding on/off precision, the two-timeline
redundant-insert interaction, tolerance actually catching a strict
mismatch and correctly tolerating a rounded one, and 3 of the 7
individual checks each independently reproducing the exact bug their
bundled counterpart already catches). 433 tests total, Debug and Release
clean.

## Layer isolation ("solo")

Real request, with the exact design already specified: an "Iso" button
next to each layer that shows only that layer (hiding the rest); click
another layer's Iso to add it to what's visible; click an isolated
layer's Iso again to remove it from isolation; a "Show all layers"
button (or un-isolating the last one) exits isolation and restores
everything. Standard "solo" pattern from Blender's Local View and
similar DCC tools -- no design changes needed, just implementation.

**Built entirely on the existing hide primitives** (`editor/Visibility.h`,
from the earlier hide-layers/paths/groups work) -- isolation isn't a
separate concept with its own visibility state, it's just an
`EditorUI`-private `std::set<int> isolatedLayers_` (which layer numbers
are currently isolated) that decides how to call `setLayerHidden()`/
`showAllPaths()`: entering isolation hides every OTHER layer and shows
the clicked one; adding another isolated layer just un-hides it, leaving
the rest as they are; removing the last isolated layer calls
`showAllPaths()` to fully exit. Scoped per-object (`isolatedLayersObjectId_`)
so a stale isolation set from a previously active object can't misapply
to a different object's layer numbers after switching.

**Consistency with the existing per-layer Visible checkbox**: toggling
Visible directly (bypassing Iso) while isolation is active keeps
`isolatedLayers_` in sync too, so the Iso button's highlight and "Show
all layers"' enabled state never drift from what's actually shown,
regardless of which control the user used.

**Verified**: builds clean, Debug and Release, 433 tests (no new unit
test -- `isolatedLayers_` and its orchestration are `EditorUI`-private
UI-only state with no ImGui context in the test binary, same rationale
as the earlier animation-exit fix; the underlying `setLayerHidden()`/
`showAllPaths()`/`isLayerHidden()` primitives it composes are already
covered by the Visibility test suite).

## Fixed Save/Open Project dialog filter (was "gfproj.bed")

Real-use report: saving a project produced a file named with a compound
`.gfproj.bed` extension instead of plain `.gfproj`.

**Root cause**: `showSaveProjectDialog()` and `showOpenProjectDialog()`
(`src/ui/FileDialog.cpp`) both had their `lpstrFilter` copy-pasted from
`showSaveBedDialog()`/`showOpenBedDialog()` -- literally
`L"Bed settings\0*.bed\0All files\0*.*\0"` on the PROJECT dialogs. With
that filter selected (index 1, the default) and the initial filename
already carrying a `.gfproj` extension that doesn't match any pattern in
it, Windows' common file dialog appended the mismatched filter's
extension on save.

**Fix**: both filters corrected to `L"GcodeForge Project\0*.gfproj\0All files\0*.*\0"`.

**Verified**: builds clean, Debug and Release, 433 tests (Windows-native
dialog code, no unit test possible -- same category as every other
`FileDialog.cpp` function).

## Bed conform as a non-destructive, adjustable "layer"

Real request: "when I apply bed conform to an object, I should get that
saved in a layer... I can delete it or multiply it or decrease it. I can
bake it to make it part of the object." Previously `applyBedConform()`
was a pure one-time mutation of `path.from.z`/`to.z`/`speedOverride` with
zero memory of having been applied -- explicitly documented as "NOT
idempotent by design."

**New model** (`include/model/BedConformRecord.h`): `BedConformRecord`
stores, per affected path, the PRE-conform baseline (`from`/`to` Z,
effective speed) plus the delta a scale of 1.0 produces (`zDeltaFromMm`,
`zDeltaToMm`, `speedFactorDelta`), and the record's current `scale`.
Storing the baseline (not just a delta) means re-scaling is always
computed fresh from the same fixed point -- `preConformZ + scale * delta`
-- never compounds on a previous scale, so repeated adjustment can't
drift. `SceneObject::bedConform` holds at most one active record.

**New operations** (`editor/BedConform.h`): `applyBedConformRecorded()`
(replaces the old one-shot `applyBedConform`, captures the baseline and
applies at scale 1.0), `setBedConformScale()` (recompute at any scale --
this is "multiply" and "decrease," one continuous control rather than
two separate actions), `removeBedConform()` (revert to baseline exactly,
clear the record -- "delete"), `bakeBedConform()` (keep the current
values, clear the record -- "bake," makes it permanent and no longer
adjustable). Re-applying while a record is already active reverts first,
so the fresh conform is computed from the object's TRUE pre-conform
state, never stacked on top of a previous (possibly rescaled) one.

**UI** (`drawBedConformPanel`): once a conform is active, shows an
"Effect strength" drag float (0-3x) plus Delete/Bake buttons.

**Persistence** (`io/ProjectIO.cpp`): new `bedConformBegin`/`bedConformPath`/
`bedConformEnd` records save/load the full active record (flags, scale,
every per-path baseline+delta) -- "also saved with the project."

**Verified**: 16 new tests (record creation, scale up/down math against
the plain apply's own numbers, repeated re-scaling proven driftless,
delete reverting to the exact baseline, bake-then-reapply stacking
correctly on the now-permanent state, and full project round-trip) plus
every existing BedConform test unchanged. 449 tests total, Debug and
Release clean.

Still open from the same request: clicking a heightmap vertex directly
in the 3D viewport and a drag-brush for painting Z (add/remove modes,
adjustable power) -- the heightmap's Columns/Rows grid and its 3D-mesh
rendering already existed and needed no new work; the interactive
picking/brush tool itself is separate, larger work not yet started.

## Click-to-paint heightmap vertices in the 3D viewport

Follow-up to the above -- the remaining open piece. Scoped down from the
original ask (a continuous drag-brush with radius/falloff) to what the
user actually wanted once asked directly: click once per vertex, exactly
the nearest one, no falloff -- simpler and, per the user, correct.

**Picking** (`editor/Picking.h`'s new `pickNearestHeightmapVertex()`):
same brute-force screen-space-projection approach `pickNearestPath()`
already uses, applied to every heightmap grid vertex. Critically, each
vertex's WORLD position mirrors `BedHeightmapRenderer`'s own vertex-build
formula EXACTLY (bed origin/size + that vertex's OWN current elevation)
-- a click has to land on the vertex as it's actually rendered, bumps and
all, not a flat zero-elevation approximation, or clicking a visibly
raised vertex could silently pick a different, flat one that happens to
share the same X/Y.

**UI** (`drawBedPanel`'s heightmap section): a "Paint mode" checkbox,
Add/Remove radio buttons, and a Power (mm per click) drag float. While
active, a viewport click is intercepted BEFORE normal path selection
(`main.cpp`'s click-release handler) and nudges only the picked vertex by
+/-Power -- no radius, no falloff, exactly what was asked for.

**Verified**: 6 new tests, using a viewing angle specifically chosen so
elevation actually moves the projected screen position (a top-down
camera wouldn't exercise this at all) -- confirms clicking where a
raised vertex actually renders picks THAT vertex, not a flat neighbor at
the same X, and vice versa. 456 tests total, Debug and Release clean.

## Hide/isolate had no effect in the default (Lines) render mode

Real-use report: "the isolation button don't work."

**Root cause**: `RenderSettings::mode` defaults to `RenderMode::Lines`
(`include/render/RenderSettings.h:45`) -- but when `object.hiddenPaths`
was added (the hide-layers/paths/groups feature, and layer isolation
built on top of it), the render-side skip check only ever went into
`GeometryRenderer.cpp`, the OTHER mode. `SceneRenderer.cpp` (Lines mode),
`SelectionHighlightRenderer.cpp`, and `VertexRenderer.cpp` all kept
checking `object.visible` but never `object.hiddenPaths` at all. Anyone
using the default view -- which is everyone, until they explicitly
switch to Geometry mode -- would hide or isolate a layer and see
literally no change, since the hidden paths were still being drawn every
frame regardless.

**Fix**: added the same `object.hiddenPaths.count(path.number)` skip to
all three renderers, matching `GeometryRenderer`'s existing check.

**Verified**: builds clean, Debug and Release, 456 tests (GL-coupled
vertex-buffer-building code, no GL context in the test binary -- same
category as `GeometryRenderer`'s own untested-by-unit-test skip logic;
verified by reading each renderer's draw-build code directly and
confirming the default-mode gap was real).

## Layer Z Offset: correct a layer's measured height, with propagation choice

Real request: "I want this layer to have .205 in Z not .2, this will
affect the next layers" -- a flat, layer-based Z correction, distinct
from `editor/BedConform.h` (which samples elevation spatially, by XY
position). Clarified into four explicit modes rather than one fixed
behavior: just the one layer, that layer and everything above it
(uncapped), that layer and the next N above it (flat, full delta), or
that layer and the next N above it tapering linearly to zero.

**New module** (`editor/LayerZOffset.h`): `ZOffsetOptions{ startLayer,
deltaZMm, mode, layerCount }` and `applyLayerZOffset()`. Layers below
`startLayer` are never touched in any mode. Travel paths are left alone
(no `layer` number of their own), matching `BedConform`'s established
precedent for the same reason.

**UI** (`drawLayerTablePanel`'s new "Z Offset" section): starting layer,
delta Z, a 4-item mode combo, and an N field that only enables for the
two modes that use it -- kept deliberately simple/flat (no nested
options) per the explicit ask that "the UI needs to be easy to
understand and operate."

**Verified**: 16 new tests, one per mode's exact weight math (including
the taper's fractional weights at each layer and the flat cutoff's
"one layer past N is untouched, no partial effect" edge). 472 tests
total, Debug and Release clean.

## Heightmap paint tool: "nothing happens" + live vertex highlight

Real-use report: clicking with paint mode on had no visible effect, plus
a redesign request -- one modifier key (not a separate Add/Remove mode
toggle), and "any way to have a selectable vertex" for visual feedback.

**Most likely cause of "nothing happens"**: two compounding usability
gaps, not a broken pick. (1) The pick radius (`kClickPickRadiusPixels`,
8px) was tuned for toolpath geometry, far denser on screen than a
typically-sparse heightmap grid -- realistic misses would read as "does
nothing," with zero feedback either way. (2) `heightmap.visible` (the
"Show heatmap" checkbox) is independent of paint mode -- with it off,
there's nothing to see or aim at at all.

**Fixes**:
- New `kHeightmapPaintPickRadiusPixels` (24px, vs. path-picking's 8px)
  specifically for this tool.
- Turning on Paint mode now force-enables "Show heatmap" if it was off.
- `BedHeightmapRenderer::rebuild()` gained an optional
  `highlightCol`/`highlightRow` -- that one vertex renders bright white
  instead of its heatmap color. `main.cpp` tracks the nearest vertex to
  the cursor every frame (only while paint mode is active, mirroring the
  existing hover-path-for-status-bar pattern) and rebuilds with the
  highlight whenever it changes -- a live "this is what a click would
  affect" answer, directly the "selectable vertex" ask.
- Dropped the Add/Remove radio-button mode entirely: plain click raises,
  **Ctrl+click** lowers. NOT Alt+click as originally requested -- Alt is
  already globally reserved for camera orbit/pan/zoom in this app, so an
  Alt-held click never even reaches the paint-mode code at all (it's
  intercepted by camera navigation first, a real structural conflict, not
  a preference). Ctrl matches this app's existing "Ctrl = subtract"
  convention from path selection.
- The `bedDirty`-gated rebuild (fires on every paint click) now also
  carries the current hover highlight through, so a click doesn't
  visibly flash the white marker off for a frame until the mouse moves.

**Verified**: builds clean, Debug and Release, 472 tests (GL-coupled
interaction code, no GL context in the test binary -- same category as
the picking/render-skip work already covered by `pickNearestHeightmapVertex`'s
own dedicated tests).

## Hidden paths were still selectable, and hiding many layers was slow

Real-use report: "when I hide the layer I can still select them and
affect them" -- and separately, hiding many layers one at a time was
noticeably slow.

**Selectable-when-hidden, root cause**: hiding only ever affected
rendering. Nothing in the selection path checked `hiddenPaths` at all --
`pickNearestPath()`/`pickPathsInRect()` (viewport click/marquee),
`allPathNumbers()`/`travelPathNumbers()`/`printPathNumbers()` (the bulk
select buttons), and the layer table's row click all happily selected a
hidden path, letting it be transformed/speed-edited/rotated same as any
visible one. And a path already selected BEFORE being hidden simply
stayed in `selectedPaths` -- hiding never touched the existing selection.

**Fix**: added `hiddenPaths` checks to both picking functions and to
`allPathNumbers`/`travelPathNumbers`/`printPathNumbers`, plus a filter at
the layer-table's actual selection call site (in `EditorUI.cpp`, not in
`pathNumbersForLayer()` itself -- that function is ALSO used internally
by `setLayerHidden()`/`isLayerHidden()` for hide/show bookkeeping, which
needs to see every path in a layer regardless of hidden state to
correctly un-hide it; filtering it there would have broken un-hiding).
`hidePaths()` now also erases from `object.selectedPaths` whatever it
hides, so an already-selected path can't stay selected through a hide.

**Slow bulk-hide, root cause**: each layer's Visible checkbox does a full
`undoStack.snapshotBeforeChange(scene)` (a whole-Scene deep copy,
`UndoStack.h`'s documented design) plus a full geometry rebuild.
Hiding N layers one checkbox at a time costs N snapshots and N rebuilds
of the ENTIRE scene, not just N cheap flag flips -- on a real file with
tens of thousands of paths, clicking through dozens of layers compounds
into real, felt lag.

**Fix**: new "Hide all layers" button next to the existing "Show all
layers" (which is now also usable outside isolation mode, not just to
exit it) -- one click, one snapshot, one rebuild, regardless of layer
count.

**Verified**: 6 new tests (hiding an already-selected path removes it;
each selection-producing function excludes hidden paths;
`pathNumbersForLayer` deliberately stays unfiltered; both picking
functions skip a hidden path even scored dead-center). 481 tests total,
Debug and Release clean.

## Object comments, stored inside the .src file itself

Real request: "add a comment in the file src when exporting, this will
be stored inside the src... read by GcodeForge... deleted if I want to."
`SceneObject::comment` is a new free-text field, editable in a small text
box (Object tab, "Comment" section, with a Delete button) -- but the real
work is making it survive round-trip through the ACTUAL .src file, not
just `.gfproj` project saves.

**Export** (`SrcExporter.cpp`): when `object.comment` is non-empty, a
tagged block --
```
; GCODEFORGE COMMENT START
; <line 1>
; <line 2>
; GCODEFORGE COMMENT END
```
-- is inserted right after the `DEF` line (found via the same `DEF`
pattern `ExportValidation.cpp` uses), through the exact same
`insertionsByLine` mechanism every other insertion (speed lines, layer
actions) already uses -- so coordinate-patch indices, computed before any
insertions happen, stay correct. Regenerated fresh from `object.comment`
on every export, never stored permanently in `object.sourceLines` --
editing or deleting the comment can never leave stale text behind.

**Import** (`SrcParser.cpp`): the tagged block is stripped out and its
text recovered into `object.comment` BEFORE any line-index assignment
happens -- `Path::srcLine`/`StartPoint::srcLine` are indices into
`object.sourceLines`, so the comment block has to already be gone from
that list before indices are computed, not removed afterward (which
would leave every recorded index off by however many comment lines
preceded it).

**Persistence** (`ProjectIO.cpp`): also saved in `.gfproj` (one
`commentLine` record per line, matching the existing multi-line `srcline`
pattern), so it survives a project save even before ever being exported.

**Verified**: 12 new tests -- full export/re-parse/re-export round-trip
(exact text recovered, marker lines don't leak into `sourceLines`,
re-exporting a re-parsed object writes the block exactly once, not
doubled), deletion actually removing the block, and project-file
round-trip. 494 tests total, Debug and Release clean.

## UI overhaul: real docking, icon toolbar, 7 dockable windows, new theme

Real request, in one message: "the speed tools should be on the top as a
floating tool palette with icons... the other tool should be big buttons
that can be pressed and the window will appear, object layers, bed
options, advanced speed option. Mirroring, linking. Geometry options,
lights and preview options. Animation also should get its own window,
everything is dokable... make a different ui style." Resumes and
completes the UI overhaul planned (and paused for an urgent bug) earlier
this session.

**ImGui docking migration** (`CMakeLists.txt`): the vendored ImGui was a
tagged release off `master` (`v1.91.1`), which has no docking support at
all -- bumped `GIT_TAG` to a SHA-pinned commit off ImGui's separate
`docking` branch (resolved via `git ls-remote`, pinned to the exact
commit rather than the branch name for reproducible builds, since
`docking` is continuously rebased). One real API break surfaced:
`ImGuiChildFlags_Border` was renamed to `ImGuiChildFlags_Borders`
(plural) on this branch -- fixed at both call sites.
`io.ConfigFlags |= ImGuiConfigFlags_DockingEnable` turns it on.

**New theme** (`ui/Theme.h/.cpp`): a neutral charcoal palette with one
muted-blue accent used consistently for anything on/active/draggable
(buttons, checkmarks, active tabs, docking preview), replacing
`ImGui::StyleColorsDark()` -- in the spirit of Blender/Fusion 360 rather
than ImGui's generic dark default. Deliberately leaves the semantic
severity colors (Export dialog's critical/warning red/amber,
`PathColorizer`'s speed heatmap) untouched -- those are data, not chrome.

**Icon toolbar** (`ui/Icons.h/.cpp`): 10 self-contained vector icons
(Open, Save, Undo, Redo, Move, Rotate, Frame All, Grid toggle,
Lines/Geometry render-mode toggle, a shortcut into Advanced Speed) drawn
with plain `ImDrawList` primitives -- no icon font, no image asset, no
new dependency. `IconButton()` shows an "active" state (accent-colored
background) for the current gizmo mode and render mode, so the toolbar
doubles as a status readout, not just triggers. Cross-boundary actions
(gizmo mode lives in `main.cpp`'s render loop, not `EditorUI`) go through
the same request-flag pattern as every other toolbar action
(`moveToolRequested()`, `frameAllRequested()`, etc.).

**Seven dockable windows**, replacing the old two-window/four-tab
catch-all layout (Editor: View/Scene/Object/Animate tabs, plus a
separate Bed window): "Object & Layers" (object list, cell template,
comment, transform, layer table, selection groups), "Bed", "Advanced
Speed", "Mirror & Link", "Geometry" (view/display settings, color mode,
bed conform, stats), "Lights & Preview" (lighting, newly extracted out of
`drawBedPanel` into its own `drawLightingPanel()` -- self-contained,
touched only `lighting`), and "Animation". A second toolbar row of big
launcher buttons toggles each one; `buildDefaultDockLayout()` (ImGui's
`DockBuilder*` API) pre-assigns all seven to sensible regions (left:
Object & Layers; right, tabbed together: the five less-constantly-needed
ones; bottom: Animation) on first run only, so opening a window later via
its launcher button lands it in a sensible spot instead of floating
wherever ImGui defaults to. Every existing panel-drawing function's BODY
is unchanged -- this is pure re-plumbing of which window each is called
from.

**Self-verification** (`ui/ScreenCapture.h/.cpp`): a hand-rolled 24bpp
BMP writer (`glReadPixels` + a raw BITMAPFILEHEADER/BITMAPINFOHEADER, no
PNG/stb_image_write dependency) wired to a new `GCODEFORGE_SCREENSHOT_FILE`
env var (`main.cpp`, parallel to the existing `GCODEFORGE_TEST_FILE`
diagnostic) -- dumps a screenshot after 5 frames (letting the dock layout
settle) and exits, so a UI change like this one can be visually checked
without a human launching the app by hand. Used to verify this exact
change: docking works, the theme is consistent (accent color on active
tab/title bar/checkboxes/move-tool), icons render distinctly, and the
launcher row's labels are legible.

**Verified**: full clean rebuild (Debug and Release, `--clean-first`, to
catch the same glob-cache staleness that bit `ScreenCapture.cpp`
mid-session) succeeds and links; 494 tests pass unchanged (pure
presentation-layer change, touches nothing tested by the logic suite);
self-captured screenshots confirm the toolbar, launcher row, docked
"Object & Layers" window, and theme all render correctly. NOT verified
by this session: actually dragging a window to a new dock location, or
clicking each of the other 6 launcher buttons interactively -- that needs
the user's own hands-on click-through, which a screenshot-after-5-frames
diagnostic can't substitute for.
