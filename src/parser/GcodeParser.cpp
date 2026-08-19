#include "parser/GcodeParser.h"

#include <cmath>
#include <optional>
#include <regex>

namespace {

const std::regex kGWordRe(R"(^[Gg]\s*(\d+))");
const std::regex kParamRe(R"(([XYZIJFxyzijf])\s*([-+]?\d+(?:\.\d+)?))");

constexpr double kArcStepRadians = 5.0 * 3.14159265358979323846 / 180.0; // subdivide arcs every 5 degrees

std::string stripComment(const std::string& line) {
    std::string out;
    bool inParen = false;
    for (char c : line) {
        if (c == '(') { inParen = true; continue; }
        if (c == ')') { inParen = false; continue; }
        if (c == ';') break;
        if (!inParen) out += c;
    }
    return out;
}

struct ParsedLine {
    std::optional<int> gCode;
    std::optional<double> x, y, z, i, j, f;
};

ParsedLine parseLine(const std::string& rawLine) {
    ParsedLine result;
    std::string line = stripComment(rawLine);

    std::smatch gMatch;
    if (std::regex_search(line, gMatch, kGWordRe)) {
        result.gCode = std::stoi(gMatch[1].str());
    }

    auto begin = std::sregex_iterator(line.begin(), line.end(), kParamRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        char letter = static_cast<char>(std::toupper(static_cast<unsigned char>((*it)[1].str()[0])));
        double value = std::stod((*it)[2].str());
        switch (letter) {
            case 'X': result.x = value; break;
            case 'Y': result.y = value; break;
            case 'Z': result.z = value; break;
            case 'I': result.i = value; break;
            case 'J': result.j = value; break;
            case 'F': result.f = value; break;
            default: break;
        }
    }
    return result;
}

void appendPathSegment(SceneObject& object, glm::dvec3 from, glm::dvec3 to, PathType type,
                        const std::string& motion, std::optional<double> speed, int srcLine,
                        int& pathNo, int& layerNo, std::optional<double>& lastPrintZ) {
    ++pathNo;
    int detectedLayer = -1;
    if (type == PathType::Print) {
        if (!lastPrintZ.has_value() || std::abs(to.z - *lastPrintZ) > 1e-5) {
            ++layerNo;
            object.layers.push_back(Layer{layerNo, to.z, pathNo, pathNo});
            lastPrintZ = to.z;
        } else {
            object.layers.back().endPath = pathNo;
        }
        detectedLayer = layerNo;
    }

    Path path;
    path.number = pathNo;
    path.type = type;
    path.layer = detectedLayer;
    path.motion = motion;
    path.speed = speed;
    path.from = from;
    path.to = to;
    path.srcLine = srcLine;
    object.paths.push_back(path);
}

} // namespace

SceneObject parseGcode(const std::string& objectName, const std::vector<std::string>& lines) {
    SceneObject object;
    object.name = objectName;
    object.sourceLines = lines;

    glm::dvec3 currentPos(0.0, 0.0, 0.0);
    std::optional<double> speed;
    int pathNo = 0;
    int layerNo = 0;
    std::optional<double> lastPrintZ;

    for (int lineIndex = 0; lineIndex < static_cast<int>(lines.size()); ++lineIndex) {
        ParsedLine parsed = parseLine(lines[lineIndex]);
        if (parsed.f) speed = parsed.f;
        if (!parsed.gCode) continue;

        glm::dvec3 target = currentPos;
        if (parsed.x) target.x = *parsed.x;
        if (parsed.y) target.y = *parsed.y;
        if (parsed.z) target.z = *parsed.z;

        switch (*parsed.gCode) {
            case 0:
                appendPathSegment(object, currentPos, target, PathType::Travel, "G0", speed, lineIndex,
                                   pathNo, layerNo, lastPrintZ);
                currentPos = target;
                break;
            case 1:
                appendPathSegment(object, currentPos, target, PathType::Print, "G1", speed, lineIndex,
                                   pathNo, layerNo, lastPrintZ);
                currentPos = target;
                break;
            case 2:
            case 3: {
                // Center-offset (I/J) arc. R-form arcs aren't supported (see header note).
                if (!parsed.i && !parsed.j) break;
                const double i = parsed.i.value_or(0.0);
                const double j = parsed.j.value_or(0.0);
                const glm::dvec2 center(currentPos.x + i, currentPos.y + j);
                const glm::dvec2 start(currentPos.x, currentPos.y);
                const glm::dvec2 endXY(target.x, target.y);

                double startAngle = std::atan2(start.y - center.y, start.x - center.x);
                double endAngle = std::atan2(endXY.y - center.y, endXY.x - center.x);
                const bool clockwise = (*parsed.gCode == 2);

                double sweep = endAngle - startAngle;
                if (clockwise && sweep > 0) sweep -= 2.0 * 3.14159265358979323846;
                if (!clockwise && sweep < 0) sweep += 2.0 * 3.14159265358979323846;
                if (std::abs(sweep) < 1e-9) sweep = clockwise ? -2.0 * 3.14159265358979323846 : 2.0 * 3.14159265358979323846;

                const double radius = glm::length(start - center);
                const int steps = std::max(1, static_cast<int>(std::abs(sweep) / kArcStepRadians));

                glm::dvec3 segmentFrom = currentPos;
                for (int s = 1; s <= steps; ++s) {
                    double t = static_cast<double>(s) / static_cast<double>(steps);
                    double angle = startAngle + sweep * t;
                    glm::dvec3 segmentTo(center.x + radius * std::cos(angle),
                                          center.y + radius * std::sin(angle),
                                          currentPos.z + (target.z - currentPos.z) * t);
                    appendPathSegment(object, segmentFrom, segmentTo, PathType::Print,
                                       clockwise ? "G2" : "G3", speed, lineIndex, pathNo, layerNo, lastPrintZ);
                    segmentFrom = segmentTo;
                }
                currentPos = target;
                break;
            }
            default:
                break;
        }
    }

    return object;
}
