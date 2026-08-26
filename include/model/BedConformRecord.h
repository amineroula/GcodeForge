#pragma once

#include <optional>
#include <vector>

// One path's contribution to an active (not yet baked) bed conform,
// captured at the moment it was applied -- the PRE-conform state plus
// the delta a scale of 1.0 produces. Storing the pre-conform baseline
// (not just the delta) means the effect can be re-scaled any number of
// times with zero drift: every recompute starts from the same fixed
// point, rather than repeatedly adding/subtracting floating-point deltas
// on top of whatever the last scale left behind.
struct BedConformPathRecord {
    int pathNumber = 0;
    double preConformFromZ = 0.0;
    double preConformToZ = 0.0;
    double preConformSpeed = 0.0; // effectiveSpeed() at the moment of apply
    double zDeltaFromMm = 0.0;    // weight * elevation at `from`, scale 1.0
    double zDeltaToMm = 0.0;      // weight * elevation at `to`, scale 1.0
    double speedFactorDelta = 0.0; // effective speed factor is (1 + scale * this)
};

// A non-destructive "adjustment layer": bed conform applied to an
// object, kept re-scalable (multiply/decrease the effect strength),
// removable (revert to the pre-conform baseline), or bakeable (drop this
// record, keeping whatever the paths currently say as permanent). Only
// one active record per object at a time -- re-applying bed conform
// while one exists replaces it, computed fresh from ITS pre-conform
// baseline (see editor/BedConform.h's applyBedConformRecorded), not from
// whatever the currently-scaled state happens to be.
struct BedConformRecord {
    bool adjustZ = true;
    bool adjustSpeed = true;
    double scale = 1.0;
    std::vector<BedConformPathRecord> perPath;
};
