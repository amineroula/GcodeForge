#pragma once

#include <optional>
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

// Reads the current numeric value of an axis on a KRL motion line, or
// std::nullopt if that axis isn't present. This is what lets the exporter
// ask the question that actually matters -- "does the file's value differ
// from what we want to write?" -- rather than inferring change from the
// model, which cannot see edits applied directly to a path's coordinates.
std::optional<double> readKrlAxisValue(const std::string& line, char axisLetter);
