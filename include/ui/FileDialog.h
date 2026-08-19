#pragma once

#include <GLFW/glfw3.h>
#include <optional>
#include <string>

// A native "Open File" dialog for SRC/G-code files. Windows-only for now
// (matches GcodeForge's desktop/Studio-only scope -- see docs/PLAN.md).
// Deliberately just wraps the OS dialog rather than pulling in a
// cross-platform file-dialog library for one function.
std::optional<std::string> showOpenSrcDialog(GLFWwindow* window);
