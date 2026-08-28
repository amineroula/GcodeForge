#pragma once

#include <vector>

// A grid of measured PRINTED RESULT values (bead width in mm, as measured
// by hand with calipers on a real calibration print), entered by the
// operator -- NOT a probed bed elevation like model/BedHeightmap.h. Same
// grid mechanics (cols/rows/at/resize) as BedHeightmap on purpose (the
// bilinear-sample/taper/apply machinery in editor/PrintCalibration.h
// mirrors editor/BedConform.h closely), but a deliberately separate type:
// a heightmap is measured BEFORE printing with a probe; this is measured
// AFTER printing with calipers, and answers a different question ("did
// THIS print come out right here?" vs "is the bed physically flat here?").
// Conflating the two into one struct would blur two different real-world
// measurement processes that happen to share the same math.
//
// Real-use context: a printed calibration grid, measured post-print for
// bead width at every vertex, compared against a known "golden" target
// width the gcode was designed to produce (e.g. 3mm high x 7mm wide) --
// deviations drive an automatic per-position Z/speed correction (see
// editor/PrintCalibration.h) so the NEXT print converges on the target
// instead of the operator guessing a global fudge factor.
struct PrintCalibrationGrid {
    bool visible = false;
    int cols = 10;
    int rows = 5;
    // The target bead width THIS grid's measurements are compared against
    // -- e.g. 7.0 if the gcode was designed to print a 7mm-wide bead.
    // error at a point is (measuredWidthMm.at(col,row) - goldenWidthMm).
    double goldenWidthMm = 7.0;
    // row-major (row * cols + col), measured bead width in mm at each
    // grid point. MUST stay sized to cols*rows -- see resize().
    std::vector<float> measuredWidthMm = std::vector<float>(static_cast<size_t>(cols) * static_cast<size_t>(rows), 7.0f);

    float& at(int col, int row) { return measuredWidthMm[static_cast<size_t>(row) * cols + col]; }
    float at(int col, int row) const { return measuredWidthMm[static_cast<size_t>(row) * cols + col]; }

    // Resizes to the given column/row count, preserving existing values BY
    // GRID POSITION wherever the old and new grids overlap. New points
    // introduced by growing the grid start at goldenWidthMm (no error),
    // not 0 -- 0mm width would read as a wildly wrong measurement instead
    // of "not yet measured."
    void resize(int newCols, int newRows);
};
