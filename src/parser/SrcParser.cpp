#include "parser/SrcParser.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>

namespace {

// Same patterns as the original's index.html (motionRe, coordRe, xRe/yRe/zRe,
// velCpRe, basRe) -- kept as close to line-for-line equivalents as regex
// dialect differences allow, so future SRC edge cases can be cross-checked
// against the original source instead of re-derived from scratch.
const std::regex kMotionRe(R"(^\s*(LIN|PTP|CIRC|SPL)\b)", std::regex::icase);
const std::regex kCoordRe(R"(\{([^}]+)\})");
const std::regex kXRe(R"(\bX\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kYRe(R"(\bY\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kZRe(R"(\bZ\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kARe(R"(\bA\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kBRe(R"(\bB\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kCRe(R"(\bC\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kVelCpRe(R"(\$VEL\.CP\s*=\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kBasRe(R"(BAS\s*\(\s*#VEL_(?:CP|PTP)\s*,\s*([-+]?\d+(?:\.\d+)?)\s*\))", std::regex::icase);

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string toUpper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::optional<double> matchNumber(const std::string& text, const std::regex& re) {
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return std::stod(m[1].str());
    }
    return std::nullopt;
}

} // namespace

SceneObject parseSrc(const std::string& objectName, const std::vector<std::string>& lines) {
    SceneObject object;
    object.name = objectName;
    object.sourceLines = lines;

    // The original detects an "implicit travel section" at file start: if
    // the FIRST travel marker encountered is a ";travel end" (not a
    // ";travel start"), the file must have begun inside travel already.
    bool inTravel = false;
    for (const auto& line : lines) {
        const std::string lower = toLower(line);
        if (contains(lower, ";travel start")) { inTravel = false; break; }
        if (contains(lower, ";travel end")) { inTravel = true; break; }
    }

    std::optional<glm::dvec3> previous;
    std::optional<double> speed;
    int pathNo = 0;
    int layerNo = 0;
    std::optional<double> lastPrintZ;
    Layer* currentLayer = nullptr;

    for (int lineIndex = 0; lineIndex < static_cast<int>(lines.size()); ++lineIndex) {
        const std::string& line = lines[lineIndex];
        const std::string lower = toLower(line);

        if (contains(lower, ";travel start")) inTravel = true;

        if (auto v = matchNumber(line, kVelCpRe)) {
            speed = v;
        } else if (auto v2 = matchNumber(line, kBasRe)) {
            speed = v2;
        }

        std::smatch motionMatch;
        if (std::regex_search(line, motionMatch, kMotionRe)) {
            std::smatch coordMatch;
            if (std::regex_search(line, coordMatch, kCoordRe)) {
                const std::string block = coordMatch[1].str();
                auto x = matchNumber(block, kXRe);
                auto y = matchNumber(block, kYRe);
                auto z = matchNumber(block, kZRe);

                if (x && y && z) {
                    glm::dvec3 to(*x, *y, *z);
                    ++pathNo;
                    const PathType type = inTravel ? PathType::Travel : PathType::Print;

                    int detectedLayer = -1;
                    if (type == PathType::Print) {
                        if (!lastPrintZ.has_value() || std::abs(*z - *lastPrintZ) > 1e-5) {
                            ++layerNo;
                            object.layers.push_back(Layer{layerNo, *z, pathNo, pathNo});
                            currentLayer = &object.layers.back();
                            lastPrintZ = z;
                        }
                        detectedLayer = layerNo;
                        if (currentLayer) currentLayer->endPath = pathNo;
                    }

                    Path path;
                    path.number = pathNo;
                    path.type = type;
                    path.layer = detectedLayer;
                    path.motion = toUpper(motionMatch[1].str());
                    path.speed = speed;
                    path.from = previous.value_or(to);
                    path.to = to;
                    path.srcLine = lineIndex;
                    object.paths.push_back(path);

                    previous = to;
                }
            }
        }

        if (contains(lower, ";travel end")) inTravel = false;
    }

    return object;
}
