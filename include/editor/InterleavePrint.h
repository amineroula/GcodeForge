#pragma once

#include "model/Scene.h"

#include <optional>
#include <vector>

struct InterleaveOptions {
    // Cross-part transitions are FLAT -- they never change Z. Interleaving
    // keeps every part at the same layer height, so a horizontal move
    // between them passes through the empty gap, not over material.
    //
    // With 3+ parts in a row a direct move can still clip the part in the
    // middle. Those moves detour around in Y (still at constant Z), this
    // far clear of every part's footprint.
    double detourMarginMm = 100.0;
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
// ending at its LAST PRINT path, and the jump to the next object is a
// DIRECT horizontal move at the current layer height -- no Z change.
// This deliberately replaces whatever layer-to-layer travel the source
// file had at that point (that travel only made sense within one object).
// Only when a direct line would cross a third part does it detour around
// in Y, still without touching Z.
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

// Highest world-space Z across the given objects. Kept as a general
// scene query (the diagnostics use it); cross-part travels no longer
// need it, since they never leave the current layer height.
double highestWorldZ(const Scene& scene, const std::vector<int>& objectIds);

struct MirrorInterleaveOptions {
    int copies = 2;                  // TOTAL parts including the original
    double gapMm = 200.0;            // clear space between neighbouring copies
    double detourMarginMm = 100.0;   // Y clearance when a direct move would cross a third part
    double travelSpeed = 0.5;
};

// The whole operation in one call, because it's one intent: mirror a part
// N times AND link the copies layer by layer into a single printable
// program. Splitting it across two buttons ("mirror", then "build
// interleaved print") made the second step look optional and the first
// step look broken when used alone -- reported as "mirror doesn't work".
//
// Adds the mirrored copies to `scene` (chained with Scene::toggleLink so
// the link previews are set up too) and returns the merged interleaved
// object, which the caller adds. Returns std::nullopt if the source
// doesn't exist, has no layers, or fewer than 2 parts result.
std::optional<SceneObject> mirrorAndInterleave(Scene& scene, int sourceObjectId,
                                                const MirrorInterleaveOptions& options);

// Builds ONE merged, exportable SceneObject that prints 2+ DIFFERENT
// existing objects SEQUENTIALLY -- object[0]'s entire body (every one of
// its own layers, in its own original order, including its own internal
// travels), then a synthesized transition, then object[1]'s entire body,
// and so on. This is the real counterpart to editor/ObjectLinking.h's
// "Link->next" + bake action: a link only ever drew a preview and, once
// baked, added a single bridging travel to the from-object WITHOUT
// actually including the to-object's own paths in any exported file --
// exporting the from-object alone (the only thing Export SRC ever writes)
// silently left the to-object's geometry out entirely, even though every
// structural/speed check passed. Reported from real use: two separate
// imported objects, one correctly placed, the other never made it into
// the robot's program at all.
//
// Unlike buildInterleavedObject (round-robin per layer, each object's own
// internal travels deliberately dropped in favor of one flat cross-part
// scheme), this keeps every object's body completely intact and simply
// joins them end to end -- the natural shape for "finish part A, then
// print part B," not "cool N copies of the same part together."
//
// Every path in the result has its coordinates baked into WORLD space,
// same as buildInterleavedObject, and layer numbers are offset per object
// so two objects that each have their own "layer 1" don't collide in the
// merged layer table. Returns std::nullopt if fewer than 2 of the given
// object ids exist and have at least one detected print path.
std::optional<SceneObject> buildSequentialMergedObject(const Scene& scene, const std::vector<int>& objectIdsInOrder,
                                                         const InterleaveOptions& options);
