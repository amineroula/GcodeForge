# Port plan

Notes taken from reading the original app (`index.html`, ~6800 lines, plus
`editor-controls-v48.js`, `viewport-tools.js`, `viewport-performance.js`,
`viewport-runtime-guards.js`, `export-verification-v481.js`), and from the
product vision in [VISION.md](VISION.md). Every milestone below should trace
back to something in VISION.md — that file is the source of truth for *why*,
this file is the *how* for the C++ port. GcodeForge targets the desktop
("Studio") side only — no touch/iPad UI, no Cloudflare/PWA layer.

## Architecture

`src/` follows the pipeline in [EVOLUTION.md](EVOLUTION.md) §17 — GCode
Editor is architecturally a compiler, and the module boundaries should match:

- `parser/` — text → structured path model. No rendering/editing knowledge.
- `model/` — pure data (`Path`, `SceneObject`, `Transform`, links). No
  knowledge of parsing or rendering.
- `editor/` — mutates the model: transform, link, modify. (milestones 3, 7, 10)
- `validator/` — checks the model before export, two-tier severity: hard
  failures (missing/invalid coordinates, broken structure, unexpected path
  counts, NaN/Infinity, corrupted motion commands) vs. warnings (speed
  rounding, formatting, small numeric tolerance). (milestone 9)
- `compiler/` — serializes the model back to SRC.
- `render/` + `ui/` — camera, line renderer, LOD, ImGui panels. These
  *observe* the model; they don't own it.

## What the original does

- Loads G-code (`.gcode`/`.nc`) and KUKA `.src` files, parses them into a
  scene made of **objects**, each holding an ordered list of **paths**
  (`from`/`to` points, a `type` of `travel` or `print`, a layer index, a
  per-path speed).
- Renders the scene with a hand-rolled camera: `project(p)` in `index.html`
  rotates a point by `cam.rotX`/`cam.rotY` (two Euler angles, no real
  perspective — it's an orthographic-ish projection with manual scale/pan/zoom)
  and draws lines on a 2D `<canvas>`.
- Color modes: by object, by move type (travel vs print), by layer, by group,
  by speed bucket.
- Editing: reordering/combining objects, adjusting per-path speed, inserting
  safety movements, grouping paths.
- **Object transform**: `{x, y, z, rotZ, flipX, flipY}` per object — translate
  in 3D, rotate around Z only, flip X/Y. Applied to points via
  `transformLocalPoint()`.
- **Object linking**: `objectLinks` is a set of `"fromId->toId"` keys, toggled
  per object pair (`toggleLinkPair`, `linkActiveToNext`, `linkAllInOrder`).
  A link auto-generates a travel path connecting one object's end point to the
  next object's start point — sequences multi-object jobs.
- **Speed editing is one mechanism, not four**: every object has a
  `selectedPaths: Set<pathNumber>`. Four different UI actions all just
  populate that same set, then a single `applySpeedOverrideToPathNumbers()`
  applies exact/percent speed to whatever's selected:
  - manual click/drag selection in the viewport
  - clicking a row in the **per-object layer table** → selects all `print`
    paths on that layer (`renderLayersTable()`'s row click handler)
  - "apply to visible geometry" (`applyVisibleSpeed`) → selects everything
    currently unhidden
  - a saved **selection group** (`applySelectionGroupSpeed`) → a named,
    reusable path set with its own color
- Animation: steps a "print head" marker along the path over time
  (`requestAnimationFrame` loop advancing a `t` parameter).
- Export: writes back verified KUKA SRC (`export-verification-v481.js` checks
  the round-trip before allowing export).

## Port milestones (C++ / OpenGL)

1. **Window + GL context** — done (`src/main.cpp`).
2. **Camera** — real orbit camera using GLM (`glm::lookAt` + perspective or
   orthographic projection), replacing the hand-rolled `project()` matrix
   math. Mouse drag to orbit, scroll to zoom, matches `setView()` presets
   (top/front/right/iso).
3. **Scene data model** — `Path { glm::vec3 from, to; PathType type; int layer; float speed; std::optional<float> speedOverride; }`
   and `SceneObject { std::string name; std::vector<Path> paths; bool visible;
   Transform{x,y,z,rotZ,flipX,flipY}; std::set<int> selectedPaths;
   std::vector<SelectionGroup> selectionGroups; }`, mirroring the JS
   `objects[].paths[]` shape plus its transform/selection/group fields.
   Also `std::set<std::pair<int,int>> objectLinks` at the scene level for
   object-to-object link pairs.
4. **G-code parser** — read `.gcode`, emit `Path` list. Straight port of the
   original's line-by-line G0/G1/G2/G3 handling.
5. **KUKA SRC parser** — same, for `.src` motion lines.
6. **Line renderer** — upload paths as a `GL_LINES` vertex buffer, color by
   the active color mode (object/type/layer/group/speed), one shader.
7. **ImGui editor UI** — object list (reorder, visibility toggle, link
   toggle between adjacent objects), transform panel (X/Y/Z/rotZ/flip, plus
   quick ±50mm nudge buttons per axis), per-object layer table (click row →
   select that layer's print paths), selection-group manager
   (create/apply/delete), speed panel (exact or percent, applied to: manual
   selection / visible geometry / layer-table click / selection group — all
   four funnel through one apply function), color mode picker — functional
   equivalent of the HTML control panel. Selection supports replace/add/
   subtract modes (click = replace, shift-click = add, ctrl-click = subtract).
8. **Animation playback** — advance a `t` along the concatenated path length,
   draw a print-head marker, mirrors the JS `stepAnimation` loop. Includes
   manual step forward/back and a configurable **Next Path by N** control to
   jump N path segments at a time when reviewing long programs.
9. **SRC export + verification** — ✅ core export done (`editor/SrcExporter`):
   patches the original source lines (X/Y/Z via `applyTransform()`, `$VEL.CP`
   insertions via a two-timeline divergence check) rather than regenerating
   from the model, so anything the model doesn't fully capture (E1-E6,
   `C_VEL`, custom interrupt/safety logic, comments) is preserved
   byte-for-byte because it's never touched. Verified against a real
   24k-line production file: untouched round-trip is byte-identical. Layer
   actions (`model/LayerAction`) let the operator insert HALT/cooling/custom
   KRL commands at a layer boundary. **Not yet done**: the explicit
   hard-fail-vs-warning STRUCTURAL validation gate from the original
   (`export-verification-v481.js`) — NaN/Infinity/malformed-structure
   detection before allowing export. Current export trusts the in-memory
   model is structurally sound (reasonable today since nothing in the app
   can currently produce a malformed Path), but doesn't yet defend against
   it explicitly. Worth adding before this is treated as fully
   production-hardened.
10. **Object linking + Bake Links to Travels** — `objectLinks` generates a
    procedural travel connecting one object's end point to the next object's
    start point; this must be *cached*, not recomputed every frame (the
    original hit a real perf bug here — recalculating link geometry on every
    draw call). "Bake Links to Travels" converts an approved procedural link
    into permanent, editable `Path` data on the object, after which it's no
    different from any other travel move.
11. **Viewport LOD / adaptive rendering** — the "game-style" performance
    system: distance-based simplification, off-screen frustum culling,
    omitting screen-space-tiny segments, batching draw calls instead of one
    draw per segment. Needed once real SRC files (tens/hundreds of thousands
    of segments) are loaded — GL_LINES alone (milestone 6) will not scale to
    that without this. Selected/active geometry stays full detail regardless
    of distance.
12. **Project save/load format** — serialize objects, transforms, speed
    overrides, links, baked travels, and selection state to a project file
    (JSON is the natural choice) distinct from the SRC export, so editing
    sessions aren't lost and don't require re-deriving state from a plain SRC
    re-import.

Deferred / not in scope for GcodeForge (belongs to the web/PWA version, not
the desktop port): touch/iPad navigation vs. selection modes, freeform lasso
selection, Cloudflare Pages deployment. BedForge (Blender-based bed/process
analysis) is a separate, related project — not part of this repo.

Each milestone gets its own commit and a short writeup in `docs/LOG.md`
explaining the C++/OpenGL concepts introduced.
