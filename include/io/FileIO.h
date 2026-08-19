#pragma once

#include <string>
#include <vector>

// Reads a text file into a vector of lines (newline-stripped, handles both
// \n and \r\n). Returns an empty vector if the file couldn't be opened --
// callers check the SceneObject's line count / path count to notice.
std::vector<std::string> readLinesFromFile(const std::string& path);

// The directory containing the running .exe. Used to locate bundled assets
// (like the sample SRC file) relative to wherever the app was actually
// launched from, instead of a path baked in at compile time that would
// only exist on the machine that built it. Returns "" if it can't be
// determined.
std::string executableDirectory();
