#pragma once

#include "model/SceneObject.h"

#include <vector>

// Mirrors the original's computedOverrideValue()/applySpeedOverrideToPathNumbers():
// Exact sets an absolute speed; Reduce/Increase scale the path's CURRENT
// effective speed (override if present, else parsed) by a percentage --
// they compound on prior edits rather than reset to the original parsed
// value. PTP paths are skipped: $VEL.CP doesn't control point-to-point
// motion (see docs/PLAN.md milestone 9's export-verification note).
enum class SpeedApplyMode {
    Exact,
    Reduce,
    Increase
};

struct SpeedApplyResult {
    int appliedCount = 0;
    int skippedPtpCount = 0;
};

// value: absolute speed for Exact mode, percentage (0-100) for Reduce/Increase.
SpeedApplyResult applySpeedToPaths(SceneObject& object, const std::vector<int>& pathNumbers,
                                    SpeedApplyMode mode, double value);
