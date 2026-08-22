#include "editor/InterleavePrint.h"
#include "editor/KrlLineEdit.h"
#include "editor/MirrorObject.h"
#include "model/Transform.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <regex>

namespace {

// Does this KRL line actually carry the A axis field (with a numeric
// value)? Used to tell a genuine motion line -- which also carries B, C,
// E1-E6, always emitted together in this format -- apart from the
// synthetic "LIN {X 0,Y 0,Z 0}" fallback stub, which has none of them.
bool hasAxisFieldA(const std::string& line) {
    static const std::regex re(R"(\bA\s*[-+]?\d)");
    return std::regex_search(line, re);
}

// One object's paths for one layer, already in world space, ending at
// its last PRINT path -- the trailing/leading travels of the source
// layer are dropped, since the interleaved sequence generates its own
// flat cross-part transitions between segments instead.
struct LayerSegment {
    std::vector<Path> paths;
};

std::optional<LayerSegment> extractLayerSegment(const SceneObject& object, int layer) {
    LayerSegment segment;
    for (const auto& path : object.paths) {
        if (path.type != PathType::Print || path.layer != layer) continue;
        if (path.srcLine < 0 && path.cloneTemplateSrcLine < 0) continue; // no line to clone formatting from

        Path worldPath = path;
        worldPath.from = applyTransform(object.transform, path.from);
        worldPath.to = applyTransform(object.transform, path.to);
        segment.paths.push_back(worldPath);
    }
    if (segment.paths.empty()) return std::nullopt;
    return segment;
}

int highestLayer(const SceneObject& object) {
    int highest = 0;
    for (const auto& path : object.paths) {
        if (path.type == PathType::Print) highest = std::max(highest, path.layer);
    }
    return highest;
}

// World-space XY footprint of one object. Z is deliberately ignored:
// interleaving prints every part up to the SAME layer height together, so
// at the moment of any cross-part move all parts are the same height and
// only their XY footprints matter for whether a move would cross one.
struct Footprint {
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
};

Footprint footprintOf(const SceneObject& object) {
    Footprint f;
    f.minX = f.minY = std::numeric_limits<double>::max();
    f.maxX = f.maxY = std::numeric_limits<double>::lowest();
    for (const auto& path : object.paths) {
        for (const glm::dvec3& local : {path.from, path.to}) {
            glm::dvec3 w = applyTransform(object.transform, local);
            f.minX = std::min(f.minX, w.x); f.maxX = std::max(f.maxX, w.x);
            f.minY = std::min(f.minY, w.y); f.maxY = std::max(f.maxY, w.y);
        }
    }
    return f;
}

// Does the 2D segment a->b pass through this footprint? Liang-Barsky
// segment-vs-rectangle clipping, the same technique the marquee selection
// uses -- a segment intersects the box iff the clip interval survives.
bool segmentCrossesFootprint(const glm::dvec3& a, const glm::dvec3& b, const Footprint& f) {
    double t0 = 0.0, t1 = 1.0;
    double dx = b.x - a.x, dy = b.y - a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x - f.minX, f.maxX - a.x, a.y - f.minY, f.maxY - a.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0.0) return false; // parallel and outside this edge
            continue;
        }
        double r = q[i] / p[i];
        if (p[i] < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
        else            { if (r < t0) return false; if (r < t1) t1 = r; }
    }
    return t0 <= t1;
}

} // namespace

double highestWorldZ(const Scene& scene, const std::vector<int>& objectIds) {
    double highest = std::numeric_limits<double>::lowest();
    for (int id : objectIds) {
        for (const auto& object : scene.objects) {
            if (object.id != id) continue;
            for (const auto& path : object.paths) {
                highest = std::max(highest, applyTransform(object.transform, path.from).z);
                highest = std::max(highest, applyTransform(object.transform, path.to).z);
            }
        }
    }
    return highest == std::numeric_limits<double>::lowest() ? 0.0 : highest;
}

std::optional<SceneObject> buildInterleavedObject(const Scene& scene, const std::vector<int>& objectIdsInOrder,
                                                   const InterleaveOptions& options) {
    std::vector<const SceneObject*> objects;
    for (int id : objectIdsInOrder) {
        for (const auto& object : scene.objects) {
            if (object.id == id && highestLayer(object) > 0) objects.push_back(&object);
        }
    }
    if (objects.size() < 2) return std::nullopt;

    int maxLayer = 0;
    for (const auto* object : objects) maxLayer = std::max(maxLayer, highestLayer(*object));

    SceneObject merged;
    merged.name = objects.front()->name + " (interleaved x" + std::to_string(objects.size()) + ")";
    merged.sourceLines.push_back("DEF GCODEFORGE_INTERLEAVED()");
    merged.sourceLines.push_back("; Generated by GcodeForge: " + std::to_string(objects.size()) +
                                  " objects interleaved layer-by-layer for cooling.");
    merged.sourceLines.push_back("; The travel moves BETWEEN objects are meant to be cut apart after printing.");

    int nextPathNumber = 1;
    int currentLayerNumber = 0;
    std::optional<glm::dvec3> cursor; // where the nozzle currently is, world space
    // The speed the ROBOT is actually running at right now, as far as the
    // generated program has told it -- tracked so emit() knows when a
    // real $VEL.CP command needs writing. Reported bug: "after the file
    // is exported the speed is 0." Root cause: every path in a merged
    // object gets a genuinely real (non-synthetic) srcLine, which made
    // SrcExporter's two-timeline logic assume the file's own line already
    // asserts the correct speed "for free" -- true for a REAL file being
    // patched, but this program is built from nothing and never had any
    // $VEL.CP in it at all until this fix. The robot received no speed
    // command whatsoever and stayed at whatever it was left at (0).
    std::optional<double> currentSpeed;

    // The most recent REAL motion line seen (full A/B/C/E1-E6 axis set),
    // used as the template for synthetic travel/reposition lines instead
    // of a bare "LIN {X 0,Y 0,Z 0}" stub. Reported bug: exporting a 4-copy
    // interleave produced a file the web editor's own structural validator
    // rejected with 1630 critical issues, and the same file's points
    // failed to load on the KUKA pendant. Root cause: every synthetic
    // cross-part travel and in-layer reposition line carried ONLY X/Y/Z --
    // missing tool orientation (A/B/C) and all six extruder axes (E1-E6)
    // entirely, which both the file format and the pendant's point loader
    // require present on every motion line, not just print moves.
    std::string lastFullTemplateLine = "LIN {X 0,Y 0,Z 0,A 0,B 0,C 0,E1 0,E2 0,E3 0,E4 0,E5 0,E6 0}";

    // Appends one motion line + its Path. `templateLine` supplies the
    // format (motion command, E1-E6, C_VEL, comments) that
    // replaceKrlAxisValue then re-points at `to`; a generated travel
    // passes its own plain "LIN {...}" template instead.
    auto emit = [&](const glm::dvec3& from, const glm::dvec3& to, PathType type, int layer,
                     const std::string& templateLine, const std::string& motion, std::optional<double> speed) {
        // Write a REAL $VEL.CP command whenever the required speed
        // changes. PTP motion ignores $VEL.CP (matches SrcExporter's own
        // rule), so it's skipped there too.
        if (motion != "PTP" && speed.has_value() &&
            (!currentSpeed.has_value() || std::abs(*currentSpeed - *speed) > 1e-9)) {
            char velLine[64];
            std::snprintf(velLine, sizeof(velLine), "$VEL.CP = %.6f", *speed);
            merged.sourceLines.push_back(velLine);
            currentSpeed = speed;
        }

        std::string line = templateLine;
        line = replaceKrlAxisValue(line, 'X', to.x);
        line = replaceKrlAxisValue(line, 'Y', to.y);
        line = replaceKrlAxisValue(line, 'Z', to.z);

        Path path;
        path.number = nextPathNumber++;
        path.from = from;
        path.to = to;
        path.type = type;
        path.layer = layer;
        path.motion = motion;
        path.speed = speed;
        path.srcLine = static_cast<int>(merged.sourceLines.size());
        merged.sourceLines.push_back(line);
        merged.paths.push_back(path);
        cursor = to;
    };

    // Footprints of every part, for deciding whether a direct move would
    // cross one. Computed once -- they don't change during the build.
    std::vector<Footprint> footprints;
    footprints.reserve(objects.size());
    for (const auto* object : objects) footprints.push_back(footprintOf(*object));

    // Overall Y extent, used as the detour lane when a direct move WOULD
    // cross a part. Routing around in Y keeps the whole move at constant
    // Z, which is the point.
    double allMinY = std::numeric_limits<double>::max();
    double allMaxY = std::numeric_limits<double>::lowest();
    for (const auto& f : footprints) { allMinY = std::min(allMinY, f.minY); allMaxY = std::max(allMaxY, f.maxY); }

    // FLAT transition -- no Z movement at all.
    //
    // The earlier version lifted to a clearance height, crossed, and
    // descended. That was over-engineering born of not understanding the
    // process: interleaving prints every part up to the SAME layer height
    // together, so at the moment of a cross-part move every part is
    // exactly as tall as the nozzle is high. A straight horizontal move
    // passes through the empty gap BETWEEN parts, never over material.
    // The lift only added travel time and another chance to string.
    //
    // The one real hazard is 3+ parts in a row: going from the far part
    // back to the first would pass straight through the middle one. That
    // case detours around in Y -- still at constant Z -- instead of over
    // the top.
    // Builds a synthetic motion line from the last known real axis line,
    // so it keeps a full A/B/C/E1-E6 set instead of a bare X/Y/Z stub.
    // C_VEL (continuous blending) is stripped: travels should stop
    // precisely at each waypoint, not blend through the gap between parts.
    auto syntheticTemplate = [&](const std::string& comment) {
        std::string line = lastFullTemplateLine;
        size_t cvelPos = line.rfind("C_VEL");
        if (cvelPos != std::string::npos) line.erase(cvelPos);
        while (!line.empty() && line.back() == ' ') line.pop_back();
        return line + " ; " + comment;
    };

    auto emitTransition = [&](const glm::dvec3& target, size_t targetObjectIndex) {
        if (!cursor.has_value()) return;
        glm::dvec3 start = *cursor;
        const std::string travelTemplate = syntheticTemplate("GCODEFORGE INTERLEAVE TRAVEL -- cut here after printing");

        // Would a straight line clip a part that is neither where we're
        // leaving from nor where we're going?
        bool blocked = false;
        for (size_t i = 0; i < footprints.size() && !blocked; ++i) {
            if (i == targetObjectIndex) continue;
            if (segmentCrossesFootprint(start, target, footprints[i])) blocked = true;
        }

        if (blocked) {
            // Detour through a lane clear of every part, in Y, at the
            // SAME Z. Pick whichever side is nearer the current position
            // so the detour is as short as possible.
            double margin = std::max(options.detourMarginMm, 1.0);
            double laneY = (std::abs(start.y - allMinY) < std::abs(allMaxY - start.y))
                               ? allMinY - margin
                               : allMaxY + margin;
            emit(start, glm::dvec3(start.x, laneY, start.z), PathType::Travel, -1, travelTemplate, "LIN", options.travelSpeed);
            emit(*cursor, glm::dvec3(target.x, laneY, start.z), PathType::Travel, -1, travelTemplate, "LIN", options.travelSpeed);
        }
        emit(*cursor, target, PathType::Travel, -1, travelTemplate, "LIN", options.travelSpeed);
    };

    // (source object index, source layer) -> merged layer number, so a
    // layer action attached to "object A, layer 3" can be re-attached to
    // whichever merged layer that segment actually became. Without this,
    // layer actions were silently dropped by interleaving -- meaning a
    // part-cooling command set up before mirroring would vanish, which is
    // exactly the kind of silent loss that already burned one real print.
    std::map<std::pair<size_t, int>, int> mergedLayerOf;

    for (int layer = 1; layer <= maxLayer; ++layer) {
        for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
            const auto* object = objects[objectIndex];
            std::optional<LayerSegment> segment = extractLayerSegment(*object, layer);
            if (!segment.has_value()) continue; // this object has no such layer -- it just drops out of the rotation

            ++currentLayerNumber;
            mergedLayerOf[{objectIndex, layer}] = currentLayerNumber;
            const glm::dvec3& segmentStart = segment->paths.front().from;
            if (cursor.has_value()) {
                emitTransition(segmentStart, objectIndex);
            }

            for (const auto& path : segment->paths) {
                int templateLineIndex = path.srcLine >= 0 ? path.srcLine : path.cloneTemplateSrcLine;
                const std::string& templateLine =
                    (templateLineIndex >= 0 && templateLineIndex < static_cast<int>(object->sourceLines.size()))
                        ? object->sourceLines[static_cast<size_t>(templateLineIndex)]
                        : std::string("LIN {X 0,Y 0,Z 0}");
                // A gap inside a source layer (its own internal travel
                // moves were dropped above) still needs the nozzle to
                // actually get there -- but within one object at one
                // layer that's a short in-layer reposition, not a
                // cross-object flight, so it doesn't need the full
                // safe-height treatment.
                if (cursor.has_value() && glm::length(path.from - *cursor) > 1e-4) {
                    emit(*cursor, path.from, PathType::Travel, -1,
                          syntheticTemplate("GCODEFORGE in-layer reposition"), "LIN", options.travelSpeed);
                }
                if (hasAxisFieldA(templateLine)) lastFullTemplateLine = templateLine;
                emit(path.from, path.to, PathType::Print, currentLayerNumber, templateLine, path.motion, path.speed);
            }
        }
    }

    merged.sourceLines.push_back("END");
    if (merged.paths.empty()) return std::nullopt;

    // Re-attach every source object's layer actions to the merged layer
    // its segment became. Each object keeps its OWN actions -- if both a
    // part and its mirror have "cooling ON at layer 3", cooling fires
    // when each of them reaches its own layer 3, which is what per-layer
    // cooling means once the two are interleaved.
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        for (const auto& action : objects[objectIndex]->layerActions) {
            auto it = mergedLayerOf.find({objectIndex, action.layer});
            if (it == mergedLayerOf.end()) continue; // that layer never made it into the merge
            LayerAction remapped = action;
            remapped.layer = it->second;
            merged.layerActions.push_back(remapped);
        }
    }

    Layer layerInfo;
    // The merged object's layer table treats each emitted per-object
    // segment as its own layer (currentLayerNumber increments per
    // segment, not per physical Z) -- deliberate: for THIS object the
    // useful unit is "one continuous printed piece of one part," which
    // is exactly what the operator would want to select or speed-edit,
    // and it keeps the layer table readable rather than showing N
    // separate parts sharing one layer number.
    std::map<int, std::pair<int, int>> layerBounds;
    for (const auto& path : merged.paths) {
        if (path.type != PathType::Print) continue;
        auto it = layerBounds.find(path.layer);
        if (it == layerBounds.end()) {
            layerBounds[path.layer] = {path.number, path.number};
        } else {
            it->second.second = path.number;
        }
    }
    for (const auto& [layerNumber, bounds] : layerBounds) {
        layerInfo.layer = layerNumber;
        layerInfo.startPath = bounds.first;
        layerInfo.endPath = bounds.second;
        layerInfo.z = 0.0;
        for (const auto& path : merged.paths) {
            if (path.number == bounds.first) { layerInfo.z = path.to.z; break; }
        }
        merged.layers.push_back(layerInfo);
    }

    return merged;
}

std::optional<SceneObject> mirrorAndInterleave(Scene& scene, int sourceObjectId,
                                                const MirrorInterleaveOptions& options) {
    SceneObject* source = scene.findObject(sourceObjectId);
    if (!source || highestLayer(*source) <= 0) return std::nullopt;

    int copies = std::max(options.copies, 2);
    std::vector<int> ids{sourceObjectId};

    // Each copy mirrors the PREVIOUS one, so consecutive parts alternate
    // orientation and each is placed relative to the one before it --
    // that spreads them evenly in a row without separate placement logic.
    //
    // Re-look-up by id every iteration instead of holding a pointer:
    // Scene::addObject push_backs into a vector, which can reallocate and
    // invalidate any SceneObject* taken before the call.
    int previousId = sourceObjectId;
    for (int i = 1; i < copies; ++i) {
        SceneObject* previous = scene.findObject(previousId);
        if (!previous) break;
        SceneObject copy = mirrorObject(*previous, options.gapMm);
        int newId = scene.addObject(std::move(copy)).id;
        scene.toggleLink(previousId, newId);
        ids.push_back(newId);
        previousId = newId;
    }

    if (ids.size() < 2) return std::nullopt;

    InterleaveOptions interleave;
    interleave.detourMarginMm = options.detourMarginMm;
    interleave.travelSpeed = options.travelSpeed;
    return buildInterleavedObject(scene, ids, interleave);
}
