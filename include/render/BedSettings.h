#pragma once

#include <string>
#include <vector>

// The real KRL boilerplate a program needs to actually load and run on
// the robot -- safety interrupt declarations, BAS(#INITMOV,0), the
// safe-pose PTP, initial travel down to print start (the "header"); the
// retreat travel + $OUT[...]=FALSE shutdown block + END (the "footer") --
// captured once from a known-good real file and reused to "fix" an object
// that has none of its own (a plain sliced .gcode import, or a merge of
// objects that all happen to lack it). Cell-level, not part-level: the
// safety interrupts and I/O indices are specific to this robot cell, not
// to any one file, so it's saved alongside the bed and the safe point
// rather than per-object. See editor/CellTemplate.h for capture/apply.
struct CellTemplate {
    bool captured = false;
    std::vector<std::string> headerLines; // world-space text, as captured
    std::vector<std::string> footerLines;
};

// The print bed: its physical size, where it sits in world space ("bed
// movement" -- repositioning the whole bed, not an object), and how the
// reference grid on top of it is drawn.
struct BedSettings {
    float widthMm = 1000.0f;   // X extent
    float depthMm = 1000.0f;   // Y extent
    float originXMm = 0.0f;    // bed center position in world space
    float originYMm = 0.0f;
    float originZMm = 0.0f;
    float gridSpacingMm = 100.0f; // 10cm per line, matching typical bed-grid conventions
    bool showGrid = true;

    // The measured world position of the robot's joint-space safe point
    // (the "first safe position" PTP -- see model/StartPoint.h). This
    // lives on the CELL, not on the part: the same robot goes to the same
    // safe pose for every job, so entering it once and saving it with the
    // bed beats re-entering it per file.
    //
    // It cannot be derived from the SRC file. A joint pose has no
    // Cartesian coordinates, and computing them needs the arm's link
    // geometry plus $TOOL/$BASE, none of which the program carries. It IS
    // trivially readable off the pendant (Display > Actual Position >
    // Cartesian), which is where these numbers come from.
    bool safePointMeasured = false;
    float safePointXMm = 0.0f;
    float safePointYMm = 0.0f;
    float safePointZMm = 0.0f;

    CellTemplate cellTemplate;
};
