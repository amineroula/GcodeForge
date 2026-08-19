#include "io/FileIO.h"

#include <fstream>
#include <sstream>

std::vector<std::string> readLinesFromFile(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return lines;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}
