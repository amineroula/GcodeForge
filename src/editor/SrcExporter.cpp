#include "editor/SrcExporter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <regex>

namespace {

const std::regex kXRe(R"(\bX\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kYRe(R"(\bY\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kZRe(R"(\bZ\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);

// Replaces ONLY the numeric value after the axis letter (e.g. "X 291.12"
// -> "X 305.00"), leaving everything else on the line -- surrounding
// whitespace, A/B/C, E1-E6, trailing C_VEL, comments -- untouched.
std::string replaceAxisValue(const std::string& line, const std::regex& axisRe, char axisLetter, double newValue) {
    std::smatch match;
    if (!std::regex_search(line, match, axisRe)) return line; // axis not present on this line, leave it alone

    char replacement[64];
    std::snprintf(replacement, sizeof(replacement), "%c %.3f", axisLetter, newValue);

    std::string result = line;
    result.replace(static_cast<size_t>(match.position(0)), static_cast<size_t>(match.length(0)), replacement);
    return result;
}

std::string formatSpeedLine(double speed) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "$VEL.CP = %.6f", speed);
    return buffer;
}

// A synthetic path (created by editor::splitSelectedPaths(), srcLine ==
// -1) has no source line of its own to patch -- its position gets baked
// straight into a brand-new line instead (see the main insertion loop
// below). This is the line index that new content should attach to:
// the path's own srcLine when it has one, otherwise its clone template's.
int exportTargetLine(const Path& path) {
    return path.srcLine >= 0 ? path.srcLine : path.cloneTemplateSrcLine;
}

} // namespace

std::vector<std::string> buildExportedLines(const SceneObject& object, ExportResult& result) {
    std::vector<std::string> output = object.sourceLines;
    result.patchedCoordinateLines = 0;
    result.insertedSpeedLines = 0;
    result.insertedLayerActions = 0;

    // --- 1. Patch coordinates in place (doesn't change line count, so
    //        order doesn't matter and this can happen before insertions).
    //        Synthetic (split) paths are skipped here on purpose -- they
    //        don't own an existing line to patch; step 3 below bakes
    //        their position straight into a brand-new inserted line
    //        instead. ---
    for (const auto& path : object.paths) {
        if (path.srcLine < 0 || path.srcLine >= static_cast<int>(output.size())) continue;

        glm::dvec3 exportPos = applyTransform(object.transform, path.to);
        glm::dvec3 originalPos = path.to;
        bool changed = glm::length(exportPos - originalPos) > 1e-6;
        if (!changed) continue;

        std::string& line = output[static_cast<size_t>(path.srcLine)];
        line = replaceAxisValue(line, kXRe, 'X', exportPos.x);
        line = replaceAxisValue(line, kYRe, 'Y', exportPos.y);
        line = replaceAxisValue(line, kZRe, 'Z', exportPos.z);
        ++result.patchedCoordinateLines;
    }

    // --- 2. Collect insertions (speed changes + layer actions + synthetic
    //        split-path motion lines), grouped by target line index, THEN
    //        apply from the highest index down so earlier insertions
    //        never shift the position of a later one we haven't applied
    //        yet. ---
    std::map<int, std::vector<std::string>> insertionsByLine;

    // Layer actions: one KRL block inserted before the first motion line
    // of the target layer. Uses exportTargetLine() (not raw srcLine) so a
    // layer whose very first path happens to be a synthetic split piece
    // still finds a valid line to attach to.
    for (const auto& action : object.layerActions) {
        auto it = std::find_if(object.paths.begin(), object.paths.end(), [&](const Path& p) {
            return p.type == PathType::Print && p.layer == action.layer;
        });
        if (it == object.paths.end()) continue;
        int targetLine = exportTargetLine(*it);
        if (targetLine < 0) continue;

        insertionsByLine[targetLine].push_back("; GCODEFORGE LAYER ACTION: " + action.label);
        insertionsByLine[targetLine].push_back(action.krlText);
        ++result.insertedLayerActions;
    }

    // --- 3. Walk paths in FINAL FILE ORDER (object.paths' own vector
    //        order -- splitSelectedPaths() already inserts a synthetic
    //        path immediately before the sibling it was split from, so
    //        this order is exactly the order they should appear in the
    //        output) tracking the same two-timeline speed model as
    //        before: what the untouched original $VEL.CP lines naturally
    //        establish (a synthetic path has no such line of its own, so
    //        it never "asserts" a speed -- it just inherits whatever's
    //        currently in effect), and what's ACTUALLY in effect in the
    //        edited output stream. A synthetic path ALSO gets its own
    //        motion line synthesized here -- cloning its template's full
    //        line text (motion command, E1-E6, C_VEL, trailing comment --
    //        everything replaceAxisValue() doesn't touch) with just its
    //        own X/Y/Z substituted in, same as any coordinate patch. ---
    std::optional<double> previousOriginalSpeed;
    std::optional<double> outputSpeed;
    for (const auto& path : object.paths) {
        int targetLine = exportTargetLine(path);
        bool isSynthetic = path.srcLine < 0;

        if (path.motion != "PTP") { // $VEL.CP doesn't control PTP motion
            if (!isSynthetic) {
                double original = path.speed.value_or(0.0);
                bool originalLineAssertsHere = !previousOriginalSpeed.has_value() || std::abs(original - *previousOriginalSpeed) > 1e-9;
                if (originalLineAssertsHere) {
                    outputSpeed = original; // the untouched file's own (still-present) line will set this, for free
                }
                previousOriginalSpeed = original;
            }

            if (targetLine >= 0) {
                double effective = path.effectiveSpeed();
                if (!outputSpeed.has_value() || std::abs(effective - *outputSpeed) > 1e-9) {
                    insertionsByLine[targetLine].push_back(formatSpeedLine(effective));
                    ++result.insertedSpeedLines;
                    outputSpeed = effective;
                }
            }
        }

        if (isSynthetic && targetLine >= 0 && targetLine < static_cast<int>(object.sourceLines.size())) {
            glm::dvec3 exportPos = applyTransform(object.transform, path.to);
            std::string newLine = object.sourceLines[static_cast<size_t>(targetLine)];
            newLine = replaceAxisValue(newLine, kXRe, 'X', exportPos.x);
            newLine = replaceAxisValue(newLine, kYRe, 'Y', exportPos.y);
            newLine = replaceAxisValue(newLine, kZRe, 'Z', exportPos.z);
            insertionsByLine[targetLine].push_back(newLine);
        }
    }

    for (auto it = insertionsByLine.rbegin(); it != insertionsByLine.rend(); ++it) {
        int lineIndex = it->first;
        if (lineIndex < 0 || lineIndex > static_cast<int>(output.size())) continue;
        output.insert(output.begin() + lineIndex, it->second.begin(), it->second.end());
    }

    result.success = true;
    return output;
}

ExportResult exportSrcToFile(const SceneObject& object, const std::string& path) {
    ExportResult result;
    std::vector<std::string> lines = buildExportedLines(object, result);
    if (!result.success) return result;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        result.success = false;
        result.errorMessage = "Could not open file for writing: " + path;
        return result;
    }

    for (const auto& line : lines) {
        file << line << "\n";
    }

    return result;
}
