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
