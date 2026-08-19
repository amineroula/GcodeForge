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
};
