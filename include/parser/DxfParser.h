#pragma once

#include "model/SceneObject.h"

#include <string>
#include <vector>

// Imports a DXF file exported from 3ds Max (or similar) containing a set
// of closed 3D POLYLINE entities -- one per print layer, already sliced
// into horizontal cross-sections at increasing Z -- and builds a
// printable SceneObject out of them: each ring becomes one closed-loop
// print layer, connected to the next by a synthetic travel move.
//
// Real-use context: a 3ds Max spline model (a rounded "picture frame"
// donut) exported to DXF turned out to contain exactly this shape --
// dozens of small closed 4-vertex rectangles stacked ~3mm apart, each a
// genuine horizontal slice of the part (already effectively pre-sliced,
// not something GcodeForge needs to derive) -- PLUS several open
// (non-closed) POLYLINE entities that turned out to be 3ds Max's own
// internal loft/rail construction curves, not layers at all. The DXF
// "closed" flag (bit 0 of group code 70) is what tells the two apart;
// open entities are deliberately skipped.
//
// Coordinates are converted from the file's own $INSUNITS header value
// to millimeters (GcodeForge's native unit) -- unrecognized/unset units
// are assumed to already be millimeters.
//
// DXF carries no speed or tool-orientation information at all -- a
// spline is pure geometry. Both come from `options`, applied uniformly
// to every synthesized motion line, rather than guessed: a wrong but
// silent tool pose is a real safety issue on an actual robot.
//
// The resulting object has no real header/footer of its own (no
// &ACCESS, no safety interrupts, no shutdown block) -- it's built from
// nothing, same as a plain sliced .gcode import. Pair it with
// editor/CellTemplate.h's fix (capture a template from a real file once,
// then apply it here) before export.
struct DxfImportOptions {
    double printSpeedMps = 0.04;
    double travelSpeedMps = 0.5;
    double toolADegrees = 180.0;
    double toolBDegrees = 90.0;  // the "straight down" component, per real-file observation
    double toolCDegrees = 180.0;
};

SceneObject parseDxfSplineLayers(const std::string& objectName, const std::vector<std::string>& lines,
                                  const DxfImportOptions& options);
