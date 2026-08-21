#pragma once

#include "model/Scene.h"

#include <vector>

// A procedural travel connecting one object's LAST path's end point to
// another object's FIRST path's start point (world space) -- lets two
// separate objects be printed as if joined by one continuous travel
// move, without permanently touching either object's data until baked
// (see bakeLinkToTravel below). Scene::objectLinks (toggled from the
// object list's "Link->next" column) holds which pairs are currently
// linked; this just turns that into something drawable/bakeable.
struct LinkPreview {
    int fromObjectId = 0;
    int toObjectId = 0;
    glm::dvec3 worldFrom{0.0};
    glm::dvec3 worldTo{0.0};
};

// One preview per entry in scene.objectLinks. Silently skips any pair
// where either object no longer exists or has no paths -- a link toggle
// can outlive an object deletion, and there's nothing sensible to draw
// for an empty object anyway.
std::vector<LinkPreview> computeLinkPreviews(const Scene& scene);

// Converts one procedural link into a REAL, permanent Path on the FROM
// object: generates a new "LIN {X ..,Y ..,Z ..}" source line, inserts it
// right after the from-object's current last path's line, and appends a
// matching Travel Path with a REAL (not synthetic) srcLine pointing at
// it -- indistinguishable to every downstream system (export, rendering,
// further edits, even a future split) from a path that was actually
// parsed from the file. Removes the pair from scene.objectLinks once
// baked, since it's no longer a pending procedural link -- it's ordinary
// path data now, editable and deletable like anything else.
// Returns false (no changes made) if either object doesn't exist or the
// FROM object has no paths for the new travel to attach after.
bool bakeLinkToTravel(Scene& scene, int fromObjectId, int toObjectId);
