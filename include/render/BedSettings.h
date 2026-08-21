#pragma once

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
};
