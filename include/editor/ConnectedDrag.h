#pragma once

#include "editor/Gizmo.h"
#include "model/SceneObject.h"

#include <vector>

// One path's drag snapshot: its from/to as they were at drag start, plus
// which endpoint(s) this particular path should move by the drag delta.
struct PathDragSnapshot {
    int pathNumber = 0;
    glm::dvec3 startFrom{0.0};
    glm::dvec3 startTo{0.0};
    bool moveFrom = false;
    bool moveTo = false;
};

// Builds the full set of paths a Start/End/Whole gizmo drag should move:
// the explicitly selected paths (per `mode`), PLUS any immediately
// adjacent UNSELECTED neighbor whose touching endpoint currently coincides
// with the moving endpoint. Without this, dragging a selected path tears
// it away from a neighbor that was visually continuous with it -- exactly
// the reported "it should be connected, not moving alone" bug. Only
// propagates one hop (the immediate neighbor), not further down the
// chain: that's what keeps Start/End meaningfully different from just
// moving the whole object, while still preserving local continuity.
std::vector<PathDragSnapshot> buildDragSnapshots(const SceneObject& object, GizmoTargetMode mode);
