#pragma once

#include "model/SceneObject.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Extracting the real, non-print boilerplate at the front and back of a
// KUKA SRC program -- safety interrupt declarations, BAS(#INITMOV,0), the
// safe-pose PTP, initial travel down to the print's own start point at the
// front; the retreat travel + $OUT[...]=FALSE shutdown block + END at the
// back. Shared by editor/InterleavePrint.cpp (which lifts an object's own
// header/footer into a merged interleaved program) and editor/CellTemplate
// (which captures one object's header/footer as a reusable per-cell
// template, and applies it to an object that has none of its own).

// The [minSrcLine, maxSrcLine] span of an object's own real PRINT paths
// (PathType::Print only -- NOT travels) -- everything before it is header
// boilerplate, everything after is footer boilerplate. std::nullopt if
// the object has no real print paths with a genuine source line at all.
//
// Deliberately Print-only, not "any Cartesian path": the approach travel
// (down to print start) and the retreat travel (lift + step back, so the
// nozzle doesn't drip on the finished part before the shutdown outputs
// fire) are themselves tracked Travel-type paths, occurring immediately
// outside the print body -- an earlier version of this function used the
// span of ALL paths regardless of type, which put the boundary AFTER the
// retreat travel too, silently excluding it from "footer boilerplate"
// entirely. That meant the retreat motion was dropped: a merged/fixed
// program went straight from the last print position to the shutdown
// $OUT commands, with no travel away from the part first.
std::optional<std::pair<int, int>> pathSrcLineSpan(const SceneObject& object);

// The literal source lines (and any Cartesian paths among them,
// coordinate-patched through the object's own transform into WORLD space)
// in [startLine, endLine), a half-open range against ONE object's own
// sourceLines.
struct BoilerplateSlice {
    std::vector<std::string> lines;
    // srcLine here is relative to `lines` (0-based within this slice), NOT
    // yet the absolute index it will end up at wherever the caller splices
    // this slice in -- the caller fixes that up once it knows where.
    std::vector<Path> paths;
};

BoilerplateSlice extractBoilerplate(const SceneObject& object, int startLine, int endLineExclusive);
