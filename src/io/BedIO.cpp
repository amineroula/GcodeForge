#include "io/BedIO.h"

#include <fstream>
#include <sstream>

bool saveBedSettings(const std::string& path, const BedSettings& bed) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file << "width " << bed.widthMm << "\n";
    file << "depth " << bed.depthMm << "\n";
    file << "originX " << bed.originXMm << "\n";
    file << "originY " << bed.originYMm << "\n";
    file << "originZ " << bed.originZMm << "\n";
    file << "gridSpacing " << bed.gridSpacingMm << "\n";
    file << "showGrid " << (bed.showGrid ? 1 : 0) << "\n";
    return true;
}

bool loadBedSettings(const std::string& path, BedSettings& bed) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    BedSettings result;
    std::string key;
    std::string line;
    bool sawAnyKey = false;
    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        double value = 0.0;
        if (!(lineStream >> key >> value)) continue;
        sawAnyKey = true;
        if (key == "width") result.widthMm = static_cast<float>(value);
        else if (key == "depth") result.depthMm = static_cast<float>(value);
        else if (key == "originX") result.originXMm = static_cast<float>(value);
        else if (key == "originY") result.originYMm = static_cast<float>(value);
        else if (key == "originZ") result.originZMm = static_cast<float>(value);
        else if (key == "gridSpacing") result.gridSpacingMm = static_cast<float>(value);
        else if (key == "showGrid") result.showGrid = (value != 0.0);
    }

    if (!sawAnyKey) return false;
    bed = result;
    return true;
}
