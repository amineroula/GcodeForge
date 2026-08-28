#include "editor/PrintCalibration.h"
#include "model/Transform.h"

#include <algorithm>
#include <cmath>

double sampleWidthError(const PrintCalibrationGrid& grid, const BedSettings& bed, double worldX, double worldY) {
    if (grid.cols < 2 || grid.rows < 2) return 0.0;

    double halfWidth = bed.widthMm * 0.5;
    double halfDepth = bed.depthMm * 0.5;
    double localX = worldX - (bed.originXMm - halfWidth);
    double localY = worldY - (bed.originYMm - halfDepth);

    double spacingX = bed.widthMm / (grid.cols - 1);
    double spacingY = bed.depthMm / (grid.rows - 1);
    if (spacingX <= 0.0 || spacingY <= 0.0) return 0.0;

    double colF = std::clamp(localX / spacingX, 0.0, static_cast<double>(grid.cols - 1));
    double rowF = std::clamp(localY / spacingY, 0.0, static_cast<double>(grid.rows - 1));

    int col0 = static_cast<int>(std::floor(colF));
    int row0 = static_cast<int>(std::floor(rowF));
    int col1 = std::min(col0 + 1, grid.cols - 1);
    int row1 = std::min(row0 + 1, grid.rows - 1);
    double tx = colF - col0;
    double ty = rowF - row0;

    double w00 = grid.at(col0, row0) - grid.goldenWidthMm;
    double w10 = grid.at(col1, row0) - grid.goldenWidthMm;
    double w01 = grid.at(col0, row1) - grid.goldenWidthMm;
    double w11 = grid.at(col1, row1) - grid.goldenWidthMm;

    double top = w00 * (1.0 - tx) + w10 * tx;
    double bottom = w01 * (1.0 - tx) + w11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

namespace {
double taperWeight(int layer, int affectedLayers) {
    if (affectedLayers <= 0 || layer <= 0) return 0.0;
    double w = 1.0 - static_cast<double>(layer - 1) / affectedLayers;
    return std::clamp(w, 0.0, 1.0);
}
} // namespace

PrintCalibrationRecord applyPrintCalibrationRecorded(SceneObject& object, const PrintCalibrationGrid& grid,
                                                      const BedSettings& bed, const PrintCalibrationOptions& options) {
    PrintCalibrationRecord record;
    record.adjustZ = options.adjustZ;
    record.adjustSpeed = options.adjustSpeed;
    record.scale = 1.0;
    if (grid.cols < 2 || grid.rows < 2) return record;

    for (auto& path : object.paths) {
        if (path.type != PathType::Print) continue;
        double weight = taperWeight(path.layer, options.affectedLayers);
        if (weight <= 0.0) continue;

        PrintCalibrationPathRecord pathRecord;
        pathRecord.pathNumber = path.number;
        pathRecord.preCalibrationFromZ = path.from.z;
        pathRecord.preCalibrationToZ = path.to.z;
        pathRecord.preCalibrationSpeed = path.effectiveSpeed();

        if (options.adjustZ) {
            glm::dvec3 worldFrom = applyTransform(object.transform, path.from);
            double errorFrom = sampleWidthError(grid, bed, worldFrom.x, worldFrom.y);
            pathRecord.zDeltaFromMm = weight * options.zGainPerMmError * errorFrom;
            glm::dvec3 worldTo = applyTransform(object.transform, path.to);
            double errorTo = sampleWidthError(grid, bed, worldTo.x, worldTo.y);
            pathRecord.zDeltaToMm = weight * options.zGainPerMmError * errorTo;
        }

        if (options.adjustSpeed) {
            glm::dvec3 worldTo = applyTransform(object.transform, path.to);
            double error = sampleWidthError(grid, bed, worldTo.x, worldTo.y);
            pathRecord.speedFactorDelta = weight * options.speedGainPerMmError * error;
        }

        record.perPath.push_back(pathRecord);
    }

    // Apply at scale 1.0 immediately -- setPrintCalibrationScale() shares
    // the exact same recompute-from-baseline logic, so there's no separate
    // "first application" code path to keep in sync with re-scaling.
    object.printCalibration = record;
    setPrintCalibrationScale(object, 1.0);
    return *object.printCalibration;
}

void setPrintCalibrationScale(SceneObject& object, double newScale) {
    if (!object.printCalibration.has_value()) return;
    PrintCalibrationRecord& record = *object.printCalibration;
    record.scale = newScale;

    for (const auto& pathRecord : record.perPath) {
        Path* path = object.findPath(pathRecord.pathNumber);
        if (!path) continue; // path was deleted/split since calibration was applied

        if (record.adjustZ) {
            path->from.z = pathRecord.preCalibrationFromZ + newScale * pathRecord.zDeltaFromMm;
            path->to.z = pathRecord.preCalibrationToZ + newScale * pathRecord.zDeltaToMm;
        }
        if (record.adjustSpeed) {
            double factor = 1.0 + newScale * pathRecord.speedFactorDelta;
            factor = std::max(factor, 0.1); // never let compensation crush speed to near-zero or negative
            path->speedOverride = pathRecord.preCalibrationSpeed * factor;
        }
    }
}

void removePrintCalibration(SceneObject& object) {
    if (!object.printCalibration.has_value()) return;
    setPrintCalibrationScale(object, 0.0); // recompute back to exactly the stored pre-calibration baseline
    object.printCalibration.reset();
}

void bakePrintCalibration(SceneObject& object) {
    object.printCalibration.reset(); // current path values stay exactly as they are -- just stop tracking them as adjustable
}
