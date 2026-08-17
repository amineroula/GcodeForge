# Port plan

Notes taken from reading the original app (`index.html`, ~6800 lines, plus
`editor-controls-v48.js`, `viewport-tools.js`, `viewport-performance.js`,
`viewport-runtime-guards.js`, `export-verification-v481.js`).

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
   toggle between adjacent objects), transform panel (X/Y/Z/rotZ/flip),
   per-object layer table (click row → select that layer's print paths),
   selection-group manager (create/apply/delete), speed panel (exact or
   percent, applied to: manual selection / visible geometry / layer-table
   click / selection group — all four funnel through one apply function),
   color mode picker — functional equivalent of the HTML control panel.
8. **Animation playback** — advance a `t` along the concatenated path length,
   draw a print-head marker, mirrors the JS `stepAnimation` loop.
9. **SRC export + verification** — re-serialize edited paths to `.src`,
   port `export-verification-v481.js`'s round-trip check.

Each milestone gets its own commit and a short writeup in `docs/LOG.md`
explaining the C++/OpenGL concepts introduced.
