#pragma once

#include <vector>

// One path's contribution to an active (not yet baked) print calibration
// -- same non-destructive "adjustment layer" pattern as
// model/BedConformRecord.h (pre-calibration baseline + a scale-1.0 delta,
// so re-scaling never drifts). Kept as its own type rather than reusing
// BedConformRecord: the two are computed from different measured sources
// (probed bed elevation vs. measured printed bead width) and could
// plausibly be active on the same object at once in the future.
struct PrintCalibrationPathRecord {
    int pathNumber = 0;
    double preCalibrationFromZ = 0.0;
    double preCalibrationToZ = 0.0;
    double preCalibrationSpeed = 0.0; // effectiveSpeed() at the moment of apply
    double zDeltaFromMm = 0.0;        // weight * zGain * widthError at `from`, scale 1.0
    double zDeltaToMm = 0.0;          // weight * zGain * widthError at `to`, scale 1.0
    double speedFactorDelta = 0.0;    // effective speed factor is (1 + scale * this)
};

// A non-destructive "adjustment layer" for print calibration -- re-scalable
// (multiply/decrease the effect strength), removable (revert to the
// pre-calibration baseline), or bakeable (drop this record, keeping
// whatever the paths currently say as permanent). Only one active record
// per object at a time.
struct PrintCalibrationRecord {
    bool adjustZ = true;
    bool adjustSpeed = true;
    double scale = 1.0;
    std::vector<PrintCalibrationPathRecord> perPath;
};
