#include "editor/MirrorObject.h"

#include <algorithm>
#include <limits>

SceneObject mirrorObject(const SceneObject& source, double safeDistanceMm) {
    SceneObject mirror = source; // copies paths, sourceLines, layers, layerActions, etc.
    mirror.name = source.name + " (copy)";
    mirror.id = 0; // Scene::addObject() assigns a real id
    mirror.selectedPaths.clear();
    mirror.selectionGroups.clear();

    double minLocalX = std::numeric_limits<double>::max();
    double maxLocalX = std::numeric_limits<double>::lowest();
    for (const auto& path : source.paths) {
        minLocalX = std::min({minLocalX, path.from.x, path.to.x});
        maxLocalX = std::max({maxLocalX, path.from.x, path.to.x});
    }
    if (source.paths.empty()) { minLocalX = 0.0; maxLocalX = 0.0; }

    // A pure translation, NOT a true mirror -- flipping used to be the
    // default here, and it was a real, reported safety bug: flipping
    // local X relocates whichever point happens to be "the layer's first
    // point" (in FILE order -- the print sequence itself never reorders)
    // to a different SIDE of the copy's own bounding box, with no
    // relationship to which side actually faces the neighboring part.
    // The interleave's cross-part transition targets that first point
    // directly (see editor/InterleavePrint.cpp's emitTransition) and
    // deliberately never checks whether a straight line to it crosses the
    // TARGET's own footprint (it can't -- the target point is always ON
    // that footprint's boundary by definition). A flipped copy could put
    // that first point on the FAR edge instead of the near one, so the
    // "direct" transition ends up dragging the nozzle across the copy's
    // own already-deposited material at full travel speed to reach it --
    // reported from real use, found via the print animation. A plain
    // translated copy keeps the SAME relative geometry as the source (just
    // shifted), so the layer's first point stays on the same side it
    // always was, and the transition approaches from the expected edge.
    //
    // Placement math: real KUKA files don't have local X starting at 0
    // (300-2700mm in the sample production file), so the offset has to
    // account for where the geometry actually IS, not just how wide it
    // is. For the copy's leftmost point to clear the source's rightmost
    // point by exactly safeDistanceMm:
    //     (minX + copyX) - (maxX + sourceX) = safeDistanceMm
    //  => copyX = sourceX + (maxX - minX) + safeDistanceMm
    mirror.transform.x = source.transform.x + (maxLocalX - minLocalX) + safeDistanceMm;
    return mirror;
}
