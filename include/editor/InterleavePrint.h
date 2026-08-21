#pragma once

#include "model/Scene.h"

#include <optional>
#include <vector>

struct InterleaveOptions {
    // The transition move between one object's layer and the next
    // object's same layer lifts to this Z (world space) before crossing,
    // so the nozzle can never clip a part it's flying over. Computed by
    // the caller as (highest point of any involved object) + clearance.
    double safeTravelZMm = 0.0;
    // $VEL.CP for the generated transition moves. These are non-printing
    // repositioning moves, so they run fast -- the whole point of
    // interleaving is that the OTHER part is cooling while this happens.
    double travelSpeed = 0.5;
};

// Builds ONE merged, exportable SceneObject that interleaves 2+ existing
// objects layer-by-layer in round-robin order (object[0]'s layer 1,
// object[1]'s layer 1, ..., object[N-1]'s layer 1, object[0]'s layer 2,
// ...). Built for printing a part and its copies/mirrors (see
// editor/MirrorObject.h) together, giving each part real time to cool
// between layers instead of finishing one completely before starting the
// next -- the connecting travels are meant to be physically cut apart
// after printing, leaving separate finished parts.
//
// Where the transition goes: each per-object layer segment is emitted
// ending at its LAST PRINT path, and the jump to the next object is
// synthesized as three travel moves -- straight up to safeTravelZMm,
// across at that height, then straight down to the next segment's start.
// This deliberately replaces whatever layer-to-layer travel the source
// file had at that point (that travel only made sense within one object)
// rather than printing through it, and guarantees the crossing happens
// at a known safe height instead of at layer height.
//
// If objects have different layer counts, an object that runs out simply
// drops out of the rotation; once only one is left, its remaining layers
// are appended normally.
//
// Every path in the result has its coordinates baked into WORLD space
// (via each source object's own transform) -- the merged object's own
// transform is left at identity, which sidesteps reconciling N different
// source objects' local spaces inside one combined object.
//
// Returns std::nullopt if fewer than 2 of the given object ids exist and
// have at least one detected layer.
std::optional<SceneObject> buildInterleavedObject(const Scene& scene, const std::vector<int>& objectIdsInOrder,
                                                   const InterleaveOptions& options);

// Highest world-space Z across the given objects -- what the caller adds
// a clearance margin to for InterleaveOptions::safeTravelZMm.
double highestWorldZ(const Scene& scene, const std::vector<int>& objectIds);
