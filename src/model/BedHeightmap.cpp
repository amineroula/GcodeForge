#include "model/BedHeightmap.h"

#include <algorithm>

void BedHeightmap::resize(int newCols, int newRows) {
    newCols = std::max(newCols, 2);
    newRows = std::max(newRows, 2);

    std::vector<float> newElevations(static_cast<size_t>(newCols) * static_cast<size_t>(newRows), 0.0f);
    int copyCols = std::min(cols, newCols);
    int copyRows = std::min(rows, newRows);
    for (int row = 0; row < copyRows; ++row) {
        for (int col = 0; col < copyCols; ++col) {
            // Bounds-checked against the ACTUAL current vector size, not
            // just the declared cols/rows -- defense in depth against the
            // two ever drifting out of sync again (see the header comment
            // on elevationsMm for the bug this guards against).
            size_t oldIndex = static_cast<size_t>(row) * cols + col;
            if (oldIndex < elevationsMm.size()) {
                newElevations[static_cast<size_t>(row) * newCols + col] = elevationsMm[oldIndex];
            }
        }
    }

    cols = newCols;
    rows = newRows;
    elevationsMm = std::move(newElevations);
}
