#include "io/BedIO.h"

#include <fstream>
#include <sstream>

bool saveBedSettings(const std::string& path, const BedSettings& bed, const BedHeightmap& heightmap) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file << "width " << bed.widthMm << "\n";
    file << "depth " << bed.depthMm << "\n";
    file << "originX " << bed.originXMm << "\n";
    file << "originY " << bed.originYMm << "\n";
    file << "originZ " << bed.originZMm << "\n";
    file << "gridSpacing " << bed.gridSpacingMm << "\n";
    file << "showGrid " << (bed.showGrid ? 1 : 0) << "\n";

    file << "heightmapSpacing " << heightmap.spacingMm << "\n";
    file << "heightmapVisible " << (heightmap.visible ? 1 : 0) << "\n";
    file << "heightmapCols " << heightmap.cols << "\n";
    file << "heightmapRows " << heightmap.rows << "\n";
    // One line per row so the file stays readable/editable by hand if
    // needed, rather than one giant space-separated blob.
    for (int row = 0; row < heightmap.rows; ++row) {
        file << "heightmapRow";
        for (int col = 0; col < heightmap.cols; ++col) {
            file << " " << heightmap.at(col, row);
        }
        file << "\n";
    }
    return true;
}

bool loadBedSettings(const std::string& path, BedSettings& bed, BedHeightmap& heightmap) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    BedSettings resultBed;
    BedHeightmap resultHeightmap;
    int declaredCols = 0, declaredRows = 0;
    std::vector<float> rowValues;
    int rowsRead = 0;

    std::string key;
    std::string line;
    bool sawAnyKey = false;
    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        if (!(lineStream >> key)) continue;

        if (key == "heightmapRow") {
            float value = 0.0f;
            while (lineStream >> value) rowValues.push_back(value);
            ++rowsRead;
            continue;
        }

        double value = 0.0;
        if (!(lineStream >> value)) continue;
        sawAnyKey = true;
        if (key == "width") resultBed.widthMm = static_cast<float>(value);
        else if (key == "depth") resultBed.depthMm = static_cast<float>(value);
        else if (key == "originX") resultBed.originXMm = static_cast<float>(value);
        else if (key == "originY") resultBed.originYMm = static_cast<float>(value);
        else if (key == "originZ") resultBed.originZMm = static_cast<float>(value);
        else if (key == "gridSpacing") resultBed.gridSpacingMm = static_cast<float>(value);
        else if (key == "showGrid") resultBed.showGrid = (value != 0.0);
        else if (key == "heightmapSpacing") resultHeightmap.spacingMm = static_cast<float>(value);
        else if (key == "heightmapVisible") resultHeightmap.visible = (value != 0.0);
        else if (key == "heightmapCols") declaredCols = static_cast<int>(value);
        else if (key == "heightmapRows") declaredRows = static_cast<int>(value);
    }

    if (!sawAnyKey) return false;

    // Only trust the saved grid data if the row count we actually read
    // matches what the file declared and every row's cell count adds up --
    // a partially-written or hand-edited file falls back to an empty grid
    // (BedHeightmap::resizeToBed at the call site regenerates one at 0mm)
    // rather than reading garbage or crashing on an out-of-range index.
    if (declaredCols > 0 && declaredRows > 0 && rowsRead == declaredRows &&
        rowValues.size() == static_cast<size_t>(declaredCols) * declaredRows) {
        resultHeightmap.cols = declaredCols;
        resultHeightmap.rows = declaredRows;
        resultHeightmap.elevationsMm = std::move(rowValues);
    }

    bed = resultBed;
    heightmap = resultHeightmap;
    return true;
}
