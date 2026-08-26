#include "editor/BedConform.h"
#include "model/Transform.h"

#include <algorithm>
#include <cmath>

double sampleBedElevation(const BedHeightmap& heightmap, const BedSettings& bed, double worldX, double worldY) {
    if (heightmap.cols < 2 || heightmap.rows < 2) return 0.0;

    double halfWidth = bed.widthMm * 0.5;
    double halfDepth = bed.depthMm * 0.5;
    double localX = worldX - (bed.originXMm - halfWidth);
    double localY = worldY - (bed.originYMm - halfDepth);

    double spacingX = bed.widthMm / (heightmap.cols - 1);
    double spacingY = bed.depthMm / (heightmap.rows - 1);
    if (spacingX <= 0.0 || spacingY <= 0.0) return 0.0;

    double colF = std::clamp(localX / spacingX, 0.0, static_cast<double>(heightmap.cols - 1));
    double rowF = std::clamp(localY / spacingY, 0.0, static_cast<double>(heightmap.rows - 1));

    int col0 = static_cast<int>(std::floor(colF));
    int row0 = static_cast<int>(std::floor(rowF));
    int col1 = std::min(col0 + 1, heightmap.cols - 1);
    int row1 = std::min(row0 + 1, heightmap.rows - 1);
    double tx = colF - col0;
    double ty = rowF - row0;

    double z00 = heightmap.at(col0, row0);
    double z10 = heightmap.at(col1, row0);
    double z01 = heightmap.at(col0, row1);
    double z11 = heightmap.at(col1, row1);

    double top = z00 * (1.0 - tx) + z10 * tx;
    double bottom = z01 * (1.0 - tx) + z11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

namespace {
double taperWeight(int layer, int affectedLayers) {
    if (affectedLayers <= 0 || layer <= 0) return 0.0;
    double w = 1.0 - static_cast<double>(layer - 1) / affectedLayers;
    return std::clamp(w, 0.0, 1.0);
}
} // namespace

BedConformRecord applyBedConformRecorded(SceneObject& object, const BedHeightmap& heightmap,
                                          const BedSettings& bed, const BedConformOptions& options) {
    BedConformRecord record;
    record.adjustZ = options.adjustZ;
    record.adjustSpeed = options.adjustSpeed;
    record.scale = 1.0;
    if (heightmap.cols < 2 || heightmap.rows < 2) return record;

    for (auto& path : object.paths) {
        if (path.type != PathType::Print) continue;
        double weight = taperWeight(path.layer, options.affectedLayers);
        if (weight <= 0.0) continue;

        BedConformPathRecord pathRecord;
        pathRecord.pathNumber = path.number;
        pathRecord.preConformFromZ = path.from.z;
        pathRecord.preConformToZ = path.to.z;
        pathRecord.preConformSpeed = path.effectiveSpeed();

        if (options.adjustZ) {
            glm::dvec3 worldFrom = applyTransform(object.transform, path.from);
            pathRecord.zDeltaFromMm = weight * sampleBedElevation(heightmap, bed, worldFrom.x, worldFrom.y);
            glm::dvec3 worldTo = applyTransform(object.transform, path.to);
            pathRecord.zDeltaToMm = weight * sampleBedElevation(heightmap, bed, worldTo.x, worldTo.y);
        }

        if (options.adjustSpeed) {
            glm::dvec3 worldTo = applyTransform(object.transform, path.to);
            double elevation = sampleBedElevation(heightmap, bed, worldTo.x, worldTo.y);
            pathRecord.speedFactorDelta = weight * options.speedGainPerMm * elevation;
        }

        record.perPath.push_back(pathRecord);
    }

    // Apply at scale 1.0 immediately -- setBedConformScale() shares the
    // exact same recompute-from-baseline logic, so there's no separate
    // "first application" code path to keep in sync with re-scaling.
    object.bedConform = record;
    setBedConformScale(object, 1.0);
    return *object.bedConform;
}

void setBedConformScale(SceneObject& object, double newScale) {
    if (!object.bedConform.has_value()) return;
    BedConformRecord& record = *object.bedConform;
    record.scale = newScale;

    for (const auto& pathRecord : record.perPath) {
        Path* path = object.findPath(pathRecord.pathNumber);
        if (!path) continue; // path was deleted/split since the conform was applied -- nothing to recompute

        if (record.adjustZ) {
            path->from.z = pathRecord.preConformFromZ + newScale * pathRecord.zDeltaFromMm;
            path->to.z = pathRecord.preConformToZ + newScale * pathRecord.zDeltaToMm;
        }
        if (record.adjustSpeed) {
            double factor = 1.0 + newScale * pathRecord.speedFactorDelta;
            factor = std::max(factor, 0.1); // never let compensation crush speed to near-zero or negative
            path->speedOverride = pathRecord.preConformSpeed * factor;
        }
    }
}

void removeBedConform(SceneObject& object) {
    if (!object.bedConform.has_value()) return;
    setBedConformScale(object, 0.0); // recompute back to exactly the stored pre-conform baseline
    object.bedConform.reset();
}

void bakeBedConform(SceneObject& object) {
    object.bedConform.reset(); // current path values stay exactly as they are -- just stop tracking them as adjustable
}
