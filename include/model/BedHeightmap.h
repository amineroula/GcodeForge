#pragma once

#include <vector>

// A grid of measured bed-surface elevations (Z offset from a flat bed),
// entered by the operator from real probe/measurement data -- lets the
// bed panel show a heatmap of where the bed is warped/elevated instead of
// assuming it's perfectly flat.
//
// The operator specifies the grid directly as a column/row count (e.g.
// "10 columns, 5 rows" for a 1000mm bed), not a spacing value -- columns
// and rows ARE the source of truth here; X/Y spacing is simply derived
// from dividing the bed's width/depth by (cols-1)/(rows-1) so there's
// always a sample at both edges as well as the interior. Grid point
// (col=0, row=0) is the bed's own local origin corner (local X=0, Y=0);
// (cols-1, rows-1) is the opposite corner at local (bedWidthMm,
// bedDepthMm) -- this local frame is what gets offset by BedSettings'
// origin/extent to place each point in world space (see
// BedHeightmapRenderer::rebuild). Z is never derived -- it's purely
// whatever elevation the operator measured and entered.
struct BedHeightmap {
    bool visible = false; // whether BedHeightmapRenderer's mesh is drawn
    int cols = 10;
    int rows = 5;
    // row-major (row * cols + col), Z offset in mm at each grid point.
    // MUST stay sized to cols*rows at all times -- resize() (and every
    // caller) assumes that invariant. The default initializer below sizes
    // it to match the defaults directly above (legal: non-static member
    // initializers run in declaration order, so cols/rows are already set
    // when this one runs) -- without it, a default-constructed
    // BedHeightmap had cols=10/rows=5 but an EMPTY elevationsMm, and
    // resize()'s copy-existing-values loop indexed into that empty vector
    // out of bounds. In a Debug build that's MSVC's Debug STL raising an
    // assertion dialog with no console attached to show it on -- which
    // looks exactly like an infinite hang, not a crash, and is exactly
    // how this was originally found.
    std::vector<float> elevationsMm = std::vector<float>(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0.0f);

    float& at(int col, int row) { return elevationsMm[static_cast<size_t>(row) * cols + col]; }
    float at(int col, int row) const { return elevationsMm[static_cast<size_t>(row) * cols + col]; }

    // Resizes to the given column/row count, preserving existing values
    // BY GRID POSITION wherever the old and new grids overlap -- growing
    // or shrinking either dimension doesn't discard previously entered
    // measurements at points that still exist at the same (col, row).
    void resize(int newCols, int newRows);
};
