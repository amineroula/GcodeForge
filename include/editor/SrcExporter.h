#pragma once

#include "model/SceneObject.h"

#include <string>
#include <vector>

// Writes an edited SceneObject back out as a KUKA SRC file.
//
// Design: PATCH the original source lines, don't regenerate them from
// scratch. Our Path model doesn't capture every field a real SRC line can
// have (external axes E1-E6, the C_VEL continuous-motion blending flag,
// custom interrupt/safety logic, comments, disclaimers -- all present in
// real Eidos-sliced files) -- regenerating each motion line purely from
// the model would silently drop whatever it doesn't know about. Patching
// means every line we don't specifically understand is preserved
// byte-for-byte, because it's never touched.
//
// What actually gets patched, per line:
//  - A motion line's X/Y/Z numbers, replaced with applyTransform()'s
//    current result, ONLY if the value actually changed (keeps untouched
//    lines byte-identical, and keeps the diff meaningful when it isn't).
//  - A new "$VEL.CP = ..." line inserted before a motion line, whenever
//    the path's effective speed differs from whatever speed is already in
//    effect at that point in the file -- mirrors the original app's own
//    approach and the real file's existing pattern. PTP paths are skipped
//    ($VEL.CP doesn't control point-to-point motion).
//  - A LayerAction's KRL text, inserted before the first motion line of
//    its layer.
struct ExportResult {
    bool success = false;
    std::string errorMessage;
    int patchedCoordinateLines = 0;
    int insertedSpeedLines = 0;
    int insertedLayerActions = 0;
};

// Builds the patched line list without touching disk -- exposed
// separately from exportSrcToFile so the patching logic itself is
// testable without file I/O.
std::vector<std::string> buildExportedLines(const SceneObject& object, ExportResult& result);

ExportResult exportSrcToFile(const SceneObject& object, const std::string& path);
