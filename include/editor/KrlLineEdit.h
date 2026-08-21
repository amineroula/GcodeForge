#pragma once

#include <string>

// Replaces ONLY the numeric value after an axis letter on a KRL motion
// line (e.g. "X 291.12" -> "X 305.00"), leaving everything else --
// surrounding whitespace, other axes, E1-E6, trailing C_VEL, comments --
// untouched. If the axis letter isn't present on the line at all,
// returns it unchanged. axisLetter must be 'X', 'Y', or 'Z'.
//
// Shared by editor/SrcExporter (patching an existing line in place) and
// editor/InterleavePrint (cloning a line's format into a brand-new,
// merged motion sequence) -- was originally a private copy inside
// SrcExporter.cpp; pulled out once a second caller needed the exact same
// logic, rather than risking the two copies drifting apart.
std::string replaceKrlAxisValue(const std::string& line, char axisLetter, double newValue);
