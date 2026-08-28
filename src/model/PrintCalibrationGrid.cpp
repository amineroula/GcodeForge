#include "model/PrintCalibrationGrid.h"

#include <algorithm>

void PrintCalibrationGrid::resize(int newCols, int newRows) {
    newCols = std::max(newCols, 2);
    newRows = std::max(newRows, 2);

    std::vector<float> newWidths(static_cast<size_t>(newCols) * static_cast<size_t>(newRows),
                                  static_cast<float>(goldenWidthMm));
    int copyCols = std::min(cols, newCols);
    int copyRows = std::min(rows, newRows);
    for (int row = 0; row < copyRows; ++row) {
        for (int col = 0; col < copyCols; ++col) {
            size_t oldIndex = static_cast<size_t>(row) * cols + col;
            if (oldIndex < measuredWidthMm.size()) {
                newWidths[static_cast<size_t>(row) * newCols + col] = measuredWidthMm[oldIndex];
            }
        }
    }

    cols = newCols;
    rows = newRows;
    measuredWidthMm = std::move(newWidths);
}
