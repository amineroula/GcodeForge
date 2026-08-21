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

// How selected geometry is highlighted in Geometry mode (Lines mode always
// uses the wide-centerline technique in SelectionHighlightRenderer, which
// doesn't have this ambiguity -- there's no 3D volume to wrap). Exposed as
// a dropdown so the operator can pick whichever reads best on their eyes/
// monitor rather than being stuck with one hardcoded choice.
enum class SelectionStyle {
    // "Inverted hull": a second, enlarged copy of the selected mesh drawn
    // with front-face culling before the real mesh, producing a rim right
    // at the silhouette edge from any angle. See GeometryRenderer::draw().
    Outline,
    // Selected geometry glows/pulses toward white over time (unlit --
    // doesn't dim on faces angled away from every light, which is what
    // made an earlier version of this look inconsistent/"weird" half-lit);
    // everything else in Geometry mode dims down so the pulse reads
    // clearly against it. No extra mesh, just a per-vertex flag and a
    // uTime uniform in the mesh shader.
    Pulse,
    // Moving black/white diagonal "hazard tape" stripes across selected
    // geometry, unlit and driven by world position + time so the pattern
    // reads as an unmistakable moving texture from any angle -- about as
    // hard to confuse with ordinary lit geometry as this gets without an
    // actual image texture.
    Stripes,
    // The enlarged outline mesh (same one Outline uses) drawn as lines
    // instead of filled front-culled triangles -- a bright wireframe cage
    // around the selected geometry, cheap and unambiguous.
    Wireframe,
};

struct RenderSettings {
    RenderMode mode = RenderMode::Lines;
    SelectionStyle selectionStyle = SelectionStyle::Stripes;

    // Display filters -- which categories of thing are drawn at all.
    // Purely visual: hiding travels doesn't delete or exclude them from
    // export, it just declutters the viewport (a real file is mostly
    // print paths, and the travels weaving between them can bury the
    // geometry you're actually trying to look at).
    bool showPrintPaths = true;
    bool showTravels = true;
    bool showStartPoint = true; // the joint-space "first safe position", see model/StartPoint.h
    float beadWidthMm = 7.0f;   // cross-section width of the simulated extrusion bead
    float beadHeightMm = 3.0f;  // cross-section height (roughly: layer height)

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
