#include "io/BedIO.h"

#include <algorithm>
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

    file << "heightmapVisible " << (heightmap.visible ? 1 : 0) << "\n";
    file << "heightmapCols " << heightmap.cols << "\n";
    file << "heightmapRows " << heightmap.rows << "\n";
    // Every point gets its own line with an explicit LOCAL (bed-relative)
    // X/Y position -- (0,0) at one bed corner, (bed.widthMm, bed.depthMm)
    // at the opposite one -- plus its measured Z (raw elevation, not
    // world Z). X/Y are mathematically derivable from cols/rows + bed
    // size alone, but writing them out explicitly is what makes this file
    // self-describing/importable on its own, per the request, rather than
    // requiring the reader to already know the grid-generation formula.
    // Points are written row-major (col fastest) to match at(col,row)'s
    // own indexing, so loadBedSettings can just re-assign them in the
    // same order without needing to parse X/Y back into a grid index.
    if (heightmap.cols >= 2 && heightmap.rows >= 2) {
        float spacingX = bed.widthMm / static_cast<float>(heightmap.cols - 1);
        float spacingY = bed.depthMm / static_cast<float>(heightmap.rows - 1);
        for (int row = 0; row < heightmap.rows; ++row) {
            for (int col = 0; col < heightmap.cols; ++col) {
                file << "heightmapPoint " << (col * spacingX) << " " << (row * spacingY) << " " << heightmap.at(col, row) << "\n";
            }
        }
    }
    return true;
}

bool loadBedSettings(const std::string& path, BedSettings& bed, BedHeightmap& heightmap) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    BedSettings resultBed;
    BedHeightmap resultHeightmap;
    int declaredCols = 0, declaredRows = 0;
    std::vector<float> pointElevations; // parsed in file order, Z only -- X/Y are read but not needed to reconstruct the grid

    std::string key;
    std::string line;
    bool sawAnyKey = false;
    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        if (!(lineStream >> key)) continue;

        if (key == "heightmapPoint") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (lineStream >> x >> y >> z) pointElevations.push_back(z);
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
        else if (key == "heightmapVisible") resultHeightmap.visible = (value != 0.0);
        else if (key == "heightmapCols") declaredCols = static_cast<int>(value);
        else if (key == "heightmapRows") declaredRows = static_cast<int>(value);
    }

    if (!sawAnyKey) return false;

    // Only trust the saved grid data if the point count we actually read
    // matches what the file declared -- a partially-written or hand-edited
    // file falls back to an empty/default grid rather than reading
    // garbage or crashing on an out-of-range index.
    if (declaredCols >= 2 && declaredRows >= 2 &&
        pointElevations.size() == static_cast<size_t>(declaredCols) * declaredRows) {
        resultHeightmap.cols = declaredCols;
        resultHeightmap.rows = declaredRows;
        resultHeightmap.elevationsMm = std::move(pointElevations);
    } else {
        resultHeightmap.resize(resultHeightmap.cols, resultHeightmap.rows); // default-sized zero grid as a safe fallback
    }

    bed = resultBed;
    heightmap = resultHeightmap;
    return true;
}
