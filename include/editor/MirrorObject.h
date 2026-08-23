#pragma once

#include "model/SceneObject.h"

// Creates a translated duplicate of `source`: an independent copy of its
// paths/sourceLines, offset in X by the source's own local-space width
// plus `safeDistanceMm`, so the copy sits beside the original without
// overlapping (a starting point -- the Transform panel can still nudge it
// further by hand). Doesn't touch `source` or the scene; caller adds the
// result via Scene::addObject().
//
// Despite the function's name (kept to avoid an invasive rename of every
// caller/UI string), this is a plain COPY, not a true mirror -- it used
// to flip local X, which was a real, reported safety bug: flipping can
// relocate a layer's first print point to the FAR side of the copy's own
// footprint instead of the near side, so the interleave's cross-part
// transition (which targets that point directly, and deliberately never
// checks the target's own footprint -- see editor/InterleavePrint.h)
// would drag the nozzle across the copy's own just-deposited material at
// full travel speed to reach it. A plain translation keeps the same
// relative geometry as the source, so the transition always approaches
// from the expected side.
//
// Built for the "print several copies interleaved" workflow (see
// editor/InterleavePrint.h): a real LFAM/robotic-deposition technique --
// duplicate a part, print the copies layer-by-layer in alternation so
// each gets real time to cool between layers, then cut the connecting
// travel moves apart afterward to get separate finished parts.
SceneObject mirrorObject(const SceneObject& source, double safeDistanceMm);
