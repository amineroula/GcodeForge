#pragma once

#include "model/BedConformRecord.h"
#include "model/BedHeightmap.h"
#include "model/SceneObject.h"
#include "render/BedSettings.h"

// Samples BedHeightmap's measured elevation at an arbitrary WORLD-space
// (x, y) via bilinear interpolation between the four surrounding grid
// points -- lets a print be compensated for a bed that isn't perfectly
// flat, without needing a path's XY to land exactly on a measured point.
// Returns 0 if the heightmap has no valid grid (cols/rows < 2).
double sampleBedElevation(const BedHeightmap& heightmap, const BedSettings& bed, double worldX, double worldY);

struct BedConformOptions {
    // How many bottom layers (by Layer::layer, 1-based) get compensated,
    // with the effect tapering linearly from full strength at layer 1 to
    // zero at layer (affectedLayers + 1) and beyond -- bed warp matters
    // most for the first few layers; by the time enough material has
    // built up, the object's own geometry has absorbed the error.
    int affectedLayers = 1;
    bool adjustZ = true;
    bool adjustSpeed = true;
    // Effective-speed multiplier per mm of LOCAL bed elevation (already
    // weighted by the taper above) -- e.g. 0.05 means a path over a
    // +2mm high spot at full taper weight runs 10% faster; a -2mm low
    // spot runs 10% slower. Requested behavior: higher bed -> faster,
    // lower bed -> slower.
    double speedGainPerMm = 0.05;
};

// Applies bed conform to every PRINT path in `object` (travel paths are
// skipped -- their Z/speed aren't meaningful the same way) and returns a
// re-scalable, revertible BedConformRecord (model/BedConformRecord.h) --
// the caller stores it as `object.bedConform`. If a conform record is
// ALREADY active on `object`, it must be removed first (removeBedConform)
// so the fresh apply computes from the object's true pre-conform state,
// not from whatever the previous (possibly rescaled) conform left it at.
//
// Both endpoints of each affected path are re-sampled and shifted
// independently from their OWN world position, rather than shifting only
// one point and letting a shared vertex with the next path drift out of
// sync -- two paths that share a vertex (position-connected, as
// GeometryRenderer's run-detection defines it) sample the SAME world XY
// and so get the SAME Z shift, which is what keeps a run's visual
// connectivity intact after conforming.
BedConformRecord applyBedConformRecorded(SceneObject& object, const BedHeightmap& heightmap,
                                          const BedSettings& bed, const BedConformOptions& options);

// Re-scales an ACTIVE conform's effect strength (1.0 = as originally
// applied, 2.0 = double the Z shift/speed factor, 0.0 = fully reverted
// without removing the record). Recomputes every affected path from its
// stored pre-conform baseline + newScale * the stored per-path delta --
// never compounds on the previous scale, so repeated adjustment never
// drifts. No-op if `object.bedConform` isn't set.
void setBedConformScale(SceneObject& object, double newScale);

// Reverts every path this conform affected back to its exact pre-conform
// Z/speed and clears `object.bedConform`. No-op if not set.
void removeBedConform(SceneObject& object);

// Keeps the CURRENTLY COMPUTED Z/speed values exactly as they are --
// makes the conform permanent, indistinguishable from a manual edit --
// and clears `object.bedConform` so it's no longer independently
// adjustable/removable. No-op if not set.
void bakeBedConform(SceneObject& object);
