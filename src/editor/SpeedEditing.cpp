#include "editor/SpeedEditing.h"

namespace {

double computeOverrideValue(const Path& path, SpeedApplyMode mode, double value) {
    if (mode == SpeedApplyMode::Exact) return value;
    double oldSpeed = path.effectiveSpeed();
    if (mode == SpeedApplyMode::Reduce) return oldSpeed * (1.0 - value / 100.0);
    return oldSpeed * (1.0 + value / 100.0); // Increase
}

} // namespace

SpeedApplyResult applySpeedToPaths(SceneObject& object, const std::vector<int>& pathNumbers,
                                    SpeedApplyMode mode, double value) {
    SpeedApplyResult result;
    for (int number : pathNumbers) {
        Path* path = object.findPath(number);
        if (!path) continue;
        if (path->motion == "PTP") {
            ++result.skippedPtpCount;
            continue;
        }
        path->speedOverride = computeOverrideValue(*path, mode, value);
        ++result.appliedCount;
    }
    return result;
}
