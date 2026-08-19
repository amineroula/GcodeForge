#pragma once

#include "model/SceneObject.h"

#include <string>
#include <vector>

// Parses a plain G-code file (G0/G1/G2/G3 moves) into a SceneObject.
//
// Unlike SrcParser, this ISN'T a port of anything -- the original app never
// actually implements G-code parsing despite the product name (confirmed by
// grep across index.html; see docs/LOG.md milestone 4/5). This is a
// standard-subset implementation with explicit simplifying assumptions:
//
//  - G0 = travel, G1/G2/G3 = print. There's no E-axis extrusion tracking
//    (this targets large-format robotic extrusion, which typically gates
//    the nozzle with separate M-codes rather than a desktop-FDM E axis;
//    if that turns out to be wrong for a real input file, this is the
//    function to revisit).
//  - G2/G3 arcs are subdivided into short line segments (one Path per
//    segment) using the I/J center-offset form. R-form arcs aren't handled.
//  - F sets the "current speed" the same way $VEL.CP does for SRC, so both
//    parsers hand the rest of the app the same Path shape.
SceneObject parseGcode(const std::string& objectName, const std::vector<std::string>& lines);
