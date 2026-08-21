#include "editor/KrlLineEdit.h"

#include <cstdio>
#include <regex>

namespace {
const std::regex kXRe(R"(\bX\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kYRe(R"(\bY\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
const std::regex kZRe(R"(\bZ\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
} // namespace

std::string replaceKrlAxisValue(const std::string& line, char axisLetter, double newValue) {
    const std::regex* axisRe = nullptr;
    switch (axisLetter) {
        case 'X': axisRe = &kXRe; break;
        case 'Y': axisRe = &kYRe; break;
        case 'Z': axisRe = &kZRe; break;
        default: return line;
    }

    std::smatch match;
    if (!std::regex_search(line, match, *axisRe)) return line; // axis not present on this line, leave it alone

    char replacement[64];
    std::snprintf(replacement, sizeof(replacement), "%c %.3f", axisLetter, newValue);

    std::string result = line;
    result.replace(static_cast<size_t>(match.position(0)), static_cast<size_t>(match.length(0)), replacement);
    return result;
}
