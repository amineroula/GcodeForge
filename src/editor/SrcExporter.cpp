#include "editor/SrcExporter.h"
#include "editor/KrlLineEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>

namespace {

std::string formatSpeedLine(double speed, bool round4Decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), round4Decimals ? "$VEL.CP = %.4f" : "$VEL.CP = %.6f", speed);
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

std::vector<std::string> buildExportedLines(const SceneObject& object, ExportResult& result,
                                             const ExportOptions& options) {
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
        std::string& line = output[static_cast<size_t>(path.srcLine)];

        // Compare against what the LINE currently says, not against the
        // model. This was a real, shipped bug: the old test was
        // `applyTransform(transform, path.to) != path.to`, which asks
        // "does the transform move this point?" -- NOT "did this point
        // change versus the file?". With an identity transform the two
        // sides are equal by definition, so every edit applied DIRECTLY
        // to a path's coordinates (gizmo drag, connected drag, bed
        // conform, split) silently failed to export. It only ever worked
        // because the tests all happened to set a transform first.
        //
        // Reading the line's own value is also the only correct source of
        // truth: the model has no memory of what the file originally
        // said once a coordinate has been edited in place.
        std::optional<double> lineX = readKrlAxisValue(line, 'X');
        std::optional<double> lineY = readKrlAxisValue(line, 'Y');
        std::optional<double> lineZ = readKrlAxisValue(line, 'Z');

        bool changed = (lineX && std::abs(*lineX - exportPos.x) > 1e-6) ||
                       (lineY && std::abs(*lineY - exportPos.y) > 1e-6) ||
                       (lineZ && std::abs(*lineZ - exportPos.z) > 1e-6);
        if (!changed) continue;

        line = replaceKrlAxisValue(line, 'X', exportPos.x);
        line = replaceKrlAxisValue(line, 'Y', exportPos.y);
        line = replaceKrlAxisValue(line, 'Z', exportPos.z);
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
    //        everything replaceKrlAxisValue() doesn't touch) with just its
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
                // Round BEFORE comparing against outputSpeed, not after --
                // otherwise two paths that only differ beyond the 4th
                // decimal (e.g. 0.06001 and 0.05999) would each think they
                // need their own redundant $VEL.CP line even though
                // they'd round to the identical 0.0600 actually written.
                if (options.roundSpeedsTo4Decimals) effective = std::round(effective * 10000.0) / 10000.0;
                if (!outputSpeed.has_value() || std::abs(effective - *outputSpeed) > 1e-9) {
                    insertionsByLine[targetLine].push_back(formatSpeedLine(effective, options.roundSpeedsTo4Decimals));
                    ++result.insertedSpeedLines;
                    outputSpeed = effective;
                }
            }
        }

        if (isSynthetic && targetLine >= 0 && targetLine < static_cast<int>(object.sourceLines.size())) {
            glm::dvec3 exportPos = applyTransform(object.transform, path.to);
            std::string newLine = object.sourceLines[static_cast<size_t>(targetLine)];
            newLine = replaceKrlAxisValue(newLine, 'X', exportPos.x);
            newLine = replaceKrlAxisValue(newLine, 'Y', exportPos.y);
            newLine = replaceKrlAxisValue(newLine, 'Z', exportPos.z);
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

ExportResult exportSrcToFile(const SceneObject& object, const std::string& path, const ExportOptions& options) {
    ExportResult result;
    std::vector<std::string> lines = buildExportedLines(object, result, options);
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
