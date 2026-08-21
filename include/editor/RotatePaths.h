#pragma once

#include "model/SceneObject.h"

// Rotates every SELECTED path around Z, about the centroid of the
// selection itself -- not the object's own pivot. Positive angles are
// counterclockwise looking down +Z, matching Transform::rotZDegrees'
// convention (see applyTransform()), so "rotate 90" on a selection reads
// the same direction as "rotate 90" on a whole object.
//
// Operates in the object's own LOCAL space (the space Path::from/to are
// already stored in), consistent with how gizmo dragging works -- so the
// result is correct regardless of the object's current transform.
//
// Does NOT touch A/B/C tool orientation, matching the existing
// whole-object transform: rotating the object already doesn't re-derive
// orientation, and re-deriving it here would silently produce a
// different (and unverified) tool pose. Does NOT propagate to
// unselected neighbors either -- like Gizmo's Start/End target mode,
// rotating a path's endpoint can leave a gap where it used to touch an
// unselected neighbor; that's an accepted trade-off already established
// elsewhere in this app (moving one vertex of a polyline), not a new one.
//
// Does nothing if the selection is empty.
void rotateSelectedPaths(SceneObject& object, double angleDegrees);
