#pragma once

#include "editor/Gizmo.h"

// Which GPU representation the viewport uses for the scene. Both modes
// read the same Scene/ColorMode -- this only changes how it's drawn.
enum class RenderMode {
    Lines,     // infinitely-thin GL_LINES -- cheap, exact path centerlines (SceneRenderer)
    Geometry   // solid rectangular "bead" boxes approximating the deposited
               // material cross-section, print paths only; travel paths
               // still draw as thin lines so nozzle-up moves stay visible
               // (GeometryRenderer)
};

struct RenderSettings {
    RenderMode mode = RenderMode::Lines;
    float beadWidthMm = 8.0f;   // cross-section width of the simulated extrusion bead
    float beadHeightMm = 4.0f;  // cross-section height (roughly: layer height)

    // Geometry mode only: hide back-facing bead triangles (the inside
    // surface of the tube, facing away from the camera) via GPU backface
    // culling -- less visual clutter from seeing through/into geometry,
    // and less fragment work on large scenes ("depth culling for a better
    // preview"). Requires every triangle to have consistent outward
    // winding, which GeometryRenderer now guarantees by construction (see
    // appendQuad in GeometryRenderer.cpp).
    bool backfaceCulling = true;

    // Picking only (not rendering): when false (default), a click prefers
    // whichever candidate path is nearest the camera among those close
    // enough on screen to plausibly be what was clicked -- i.e. it won't
    // normally let you select a path hidden behind closer geometry at the
    // same screen position. Enabling this drops that preference, picking
    // purely by 2D screen distance regardless of what's in front --
    // "select backfacing/hidden geometry."
    bool selectBackfacing = false;

    // What the move gizmo edits when dragged -- see editor/Gizmo.h.
    // Grouped here (not a dedicated struct) since this, like the rest of
    // RenderSettings, is shared viewport/editor UI state threaded through
    // EditorUI and main.cpp's input handling together.
    GizmoTargetMode gizmoMode = GizmoTargetMode::Object;
};
