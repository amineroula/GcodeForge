#include "model/BedHeightmap.h"

#include <algorithm>

void BedHeightmap::resizeToBed(float bedWidthMm, float bedDepthMm) {
    float spacing = spacingMm > 1.0f ? spacingMm : 1.0f;
    int newCols = std::max(static_cast<int>(bedWidthMm / spacing) + 1, 2);
    int newRows = std::max(static_cast<int>(bedDepthMm / spacing) + 1, 2);

    std::vector<float> newElevations(static_cast<size_t>(newCols) * static_cast<size_t>(newRows), 0.0f);
    int copyCols = std::min(cols, newCols);
    int copyRows = std::min(rows, newRows);
    for (int row = 0; row < copyRows; ++row) {
        for (int col = 0; col < copyCols; ++col) {
            newElevations[static_cast<size_t>(row) * newCols + col] = elevationsMm[static_cast<size_t>(row) * cols + col];
        }
    }

    cols = newCols;
    rows = newRows;
    elevationsMm = std::move(newElevations);
}
