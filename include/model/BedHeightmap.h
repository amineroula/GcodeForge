#pragma once

#include <vector>

// A grid of measured bed-surface elevations (Z offset from a flat bed),
// entered by the operator from real probe/measurement data at fixed
// intervals across the plate -- lets the bed panel show a heatmap of
// where the bed is warped/elevated instead of assuming it's perfectly
// flat. Grid points align to the bed's XY extent (BedSettings::widthMm/
// depthMm), spaced `spacingMm` apart in both axes.
struct BedHeightmap {
    float spacingMm = 100.0f; // one measurement every 10cm by default, per the request
    bool visible = false;     // whether BedHeightmapRenderer's mesh is drawn
    int cols = 0;
    int rows = 0;
    std::vector<float> elevationsMm; // row-major (row * cols + col), Z offset in mm at each grid point

    float& at(int col, int row) { return elevationsMm[static_cast<size_t>(row) * cols + col]; }
    float at(int col, int row) const { return elevationsMm[static_cast<size_t>(row) * cols + col]; }

    // Recomputes cols/rows from the given bed extent and this->spacingMm
    // (cols = floor(bedWidthMm/spacing)+1, so there's always a sample at
    // both edges as well as the interior), resizing elevationsMm to
    // match. Existing values are preserved BY GRID POSITION wherever the
    // old and new grids overlap -- widening the bed or changing spacing
    // doesn't discard previously entered measurements at points that
    // still exist at the same (col, row).
    void resizeToBed(float bedWidthMm, float bedDepthMm);
};
