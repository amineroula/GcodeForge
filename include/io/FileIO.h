#pragma once

#include <string>
#include <vector>

// Reads a text file into a vector of lines (newline-stripped, handles both
// \n and \r\n). Returns an empty vector if the file couldn't be opened --
// callers check the SceneObject's line count / path count to notice.
std::vector<std::string> readLinesFromFile(const std::string& path);
