#pragma once

#include "model/SceneObject.h"

// Creates a mirrored duplicate of `source`: an independent copy of its
// paths/sourceLines, with transform.flipX toggled and transform.x offset
// by the source's own local-space width plus `safeDistanceMm`, so the
// mirror sits beside the original without overlapping (a starting point
// -- the Transform panel can still nudge it further by hand). Doesn't
// touch `source` or the scene; caller adds the result via
// Scene::addObject().
//
// Built for the "print two mirrored halves interleaved" workflow (see
// editor/InterleavePrint.h): a real LFAM/robotic-deposition technique --
// mirror a part, print both halves layer-by-layer in alternation so each
// gets real time to cool between layers, then cut the connecting travel
// moves apart afterward to get two finished, separate parts.
SceneObject mirrorObject(const SceneObject& source, double safeDistanceMm);
