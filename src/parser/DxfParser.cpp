#include "parser/DxfParser.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// DXF is a flat sequence of (group code, value) pairs, each on its own
// line. Both sides commonly carry leading/trailing whitespace for column
// alignment in files written by real CAD tools -- trimmed here once so
// every other function can compare/parse cleanly.
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

struct Pair {
    int code;
    std::string value;
};

std::vector<Pair> readPairs(const std::vector<std::string>& lines) {
    std::vector<Pair> pairs;
    pairs.reserve(lines.size() / 2);
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        std::string codeStr = trim(lines[i]);
        if (codeStr.empty()) continue;
        int code = std::atoi(codeStr.c_str());
        pairs.push_back({code, trim(lines[i + 1])});
    }
    return pairs;
}

// $INSUNITS DXF header codes -> millimeters. Anything not listed here
// (including 0 = "unitless", which real exports sometimes leave unset)
// is assumed to already be millimeters rather than silently scaling by
// an unknown/wrong factor.
double unitsToMm(int insunits) {
    switch (insunits) {
        case 1: return 25.4;    // inch
        case 2: return 304.8;   // foot
        case 4: return 1.0;     // millimeter
        case 5: return 10.0;    // centimeter
        case 6: return 1000.0;  // meter
        case 13: return 0.001;  // micron
        case 14: return 100.0;  // decimeter
        default: return 1.0;
    }
}

int findInsUnits(const std::vector<Pair>& pairs) {
    for (size_t i = 0; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 9 && pairs[i].value == "$INSUNITS") {
            return std::atoi(pairs[i + 1].value.c_str());
        }
    }
    return 0;
}

struct DxfPolyline {
    bool closed = false;
    std::vector<glm::dvec3> vertices;
};

// Entities strictly within the ENTITIES section, so BLOCKS-section
// construction geometry (light-gizmo helper blocks etc., which 3ds
// Max's exporter always includes) can never be mistaken for real
// layers, even if it happened to contain a POLYLINE too.
std::vector<DxfPolyline> parsePolylinesInEntitiesSection(const std::vector<Pair>& pairs) {
    std::vector<DxfPolyline> result;

    size_t start = pairs.size(), end = pairs.size();
    for (size_t i = 0; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 2 && pairs[i].value == "ENTITIES") {
            start = i;
            break;
        }
    }
    if (start == pairs.size()) return result;
    for (size_t i = start; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "ENDSEC") {
            end = i;
            break;
        }
    }

    size_t i = start;
    while (i < end) {
        if (pairs[i].code == 0 && pairs[i].value == "POLYLINE") {
            DxfPolyline poly;
            size_t j = i + 1;
            // Entity-level properties, up through the closed flag, until
            // the first VERTEX/SEQEND marker.
            while (j < end && !(pairs[j].code == 0 && (pairs[j].value == "VERTEX" || pairs[j].value == "SEQEND"))) {
                if (pairs[j].code == 70) poly.closed = (std::atoi(pairs[j].value.c_str()) & 1) != 0;
                ++j;
            }
            while (j < end && pairs[j].code == 0 && pairs[j].value == "VERTEX") {
                double x = 0.0, y = 0.0, z = 0.0;
                ++j;
                while (j < end && pairs[j].code != 0) {
                    if (pairs[j].code == 10) x = std::atof(pairs[j].value.c_str());
                    else if (pairs[j].code == 20) y = std::atof(pairs[j].value.c_str());
                    else if (pairs[j].code == 30) z = std::atof(pairs[j].value.c_str());
                    ++j;
                }
                poly.vertices.push_back(glm::dvec3(x, y, z));
            }
            if (j < end && pairs[j].code == 0 && pairs[j].value == "SEQEND") {
                ++j;
                while (j < end && pairs[j].code != 0) ++j;
            }
            result.push_back(std::move(poly));
            i = j;
        } else {
            ++i;
        }
    }
    return result;
}

} // namespace

SceneObject parseDxfSplineLayers(const std::string& objectName, const std::vector<std::string>& lines,
                                  const DxfImportOptions& options) {
    SceneObject object;
    object.name = objectName;

    std::vector<Pair> pairs = readPairs(lines);
    double scale = unitsToMm(findInsUnits(pairs));
    std::vector<DxfPolyline> polylines = parsePolylinesInEntitiesSection(pairs);

    // Only CLOSED polylines are real cross-section layers -- open ones
    // are construction/rail curves the loft used internally, never meant
    // to be printed (see the header comment for how this was found).
    std::vector<const DxfPolyline*> rings;
    for (const auto& poly : polylines) {
        if (poly.closed && poly.vertices.size() >= 3) rings.push_back(&poly);
    }
    std::sort(rings.begin(), rings.end(), [](const DxfPolyline* a, const DxfPolyline* b) {
        return a->vertices.front().z < b->vertices.front().z;
    });

    object.sourceLines.push_back("DEF " + objectName + "()");
    object.sourceLines.push_back("; Generated by GcodeForge from a DXF spline import.");
    object.sourceLines.push_back("; This object has no safety header/shutdown footer of its own --");
    object.sourceLines.push_back("; use the Cell Template fix (Bed panel) before exporting.");

    int nextPathNumber = 0;
    std::optional<glm::dvec3> cursor;
    std::optional<double> currentSpeed;

    // Appends one real KRL LIN line (with a full A/B/C/E1-E6 field set,
    // same completeness rule editor/ExportValidation.h enforces) and its
    // tracked Path, inserting a fresh $VEL.CP whenever the required speed
    // changes -- the exact two-timeline mechanics editor/InterleavePrint.cpp
    // uses, needed for the same reason: an object built from nothing has
    // no source $VEL.CP lines of its own to inherit a speed "for free".
    auto emit = [&](const glm::dvec3& to, PathType type, int layer, double speed) {
        if (!currentSpeed.has_value() || std::abs(*currentSpeed - speed) > 1e-9) {
            char velLine[64];
            std::snprintf(velLine, sizeof(velLine), "$VEL.CP = %.6f", speed);
            object.sourceLines.push_back(velLine);
            currentSpeed = speed;
        }

        char line[256];
        std::snprintf(line, sizeof(line),
                       "LIN {X %.3f, Y %.3f, Z %.3f, A %.3f, B %.3f, C %.3f, "
                       "E1 0.0, E2 0.0, E3 0.0, E4 0.0, E5 0.0, E6 0.0 }",
                       to.x, to.y, to.z, options.toolADegrees, options.toolBDegrees, options.toolCDegrees);

        Path path;
        path.number = ++nextPathNumber;
        path.from = cursor.value_or(to);
        path.to = to;
        path.type = type;
        path.layer = layer;
        path.motion = "LIN";
        path.speed = speed;
        path.a = options.toolADegrees;
        path.b = options.toolBDegrees;
        path.c = options.toolCDegrees;
        path.srcLine = static_cast<int>(object.sourceLines.size());
        object.sourceLines.push_back(line);
        object.paths.push_back(path);
        cursor = to;
    };

    int layerNumber = 0;
    for (const DxfPolyline* ring : rings) {
        ++layerNumber;
        std::vector<glm::dvec3> points;
        points.reserve(ring->vertices.size() + 1);
        for (const auto& v : ring->vertices) points.push_back(v * scale);
        points.push_back(points.front()); // the DXF "closed" flag implies this segment; it isn't a stored vertex

        // On the very first ring there's no prior position at all -- set
        // the cursor directly instead of emitting a travel, so the first
        // print path's `from` correctly reads as the ring's own start
        // vertex (not its own `to`, which is what an unset cursor would
        // otherwise produce). CellTemplate's header anchor reads exactly
        // this field as "the object's real print start."
        if (!cursor.has_value()) {
            cursor = points.front();
        } else if (glm::length(*cursor - points.front()) > 1e-6) {
            emit(points.front(), PathType::Travel, -1, options.travelSpeedMps);
        }

        int layerStartPath = nextPathNumber + 1;
        for (size_t k = 1; k < points.size(); ++k) {
            emit(points[k], PathType::Print, layerNumber, options.printSpeedMps);
        }

        Layer layerInfo;
        layerInfo.layer = layerNumber;
        layerInfo.z = points.front().z;
        layerInfo.startPath = layerStartPath;
        layerInfo.endPath = nextPathNumber;
        object.layers.push_back(layerInfo);
    }

    object.sourceLines.push_back("END");
    return object;
}
