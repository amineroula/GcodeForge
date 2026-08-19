#include "io/FileIO.h"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::string executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return "";

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string path(static_cast<size_t>(sizeNeeded), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), path.data(), sizeNeeded, nullptr, nullptr);

    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash);
#else
    return "";
#endif
}
