#include "editor/MirrorObject.h"

#include <algorithm>
#include <limits>

SceneObject mirrorObject(const SceneObject& source, double safeDistanceMm) {
    SceneObject mirror = source; // copies paths, sourceLines, layers, layerActions, etc.
    mirror.name = source.name + " (mirror)";
    mirror.id = 0; // Scene::addObject() assigns a real id
    mirror.selectedPaths.clear();
    mirror.selectionGroups.clear();

    double maxLocalX = std::numeric_limits<double>::lowest();
    for (const auto& path : source.paths) {
        maxLocalX = std::max({maxLocalX, path.from.x, path.to.x});
    }
    if (source.paths.empty()) maxLocalX = 0.0;

    // Placement math, derived rather than guessed -- an earlier version
    // used the part's WIDTH (maxX - minX) here and a test caught it
    // overlapping the original, because that's only correct when local X
    // happens to start at 0. Real KUKA files don't: their coordinates sit
    // wherever the cell's work envelope puts them (300-2700mm in the
    // sample production file), so the offset has to account for where the
    // geometry actually IS, not just how wide it is.
    //
    // flipX negates local X, so the source's local range [minX, maxX]
    // becomes [-maxX, -minX] before translation. For the mirror's
    // leftmost point to clear the source's rightmost point by exactly
    // safeDistanceMm:
    //     (-maxX + mirrorX) - (maxX + sourceX) = safeDistanceMm
    //  => mirrorX = sourceX + 2*maxX + safeDistanceMm
    // minX drops out entirely -- only the far edge matters.
    mirror.transform.flipX = !source.transform.flipX;
    mirror.transform.x = source.transform.x + 2.0 * maxLocalX + safeDistanceMm;
    return mirror;
}
