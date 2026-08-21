#pragma once

#include <GLFW/glfw3.h>
#include <optional>
#include <string>

// Native "Open"/"Save" dialogs. Windows-only for now (matches GcodeForge's
// desktop/Studio-only scope -- see docs/PLAN.md). Deliberately just wraps
// the OS dialog rather than pulling in a cross-platform file-dialog
// library for a handful of functions.
std::optional<std::string> showOpenSrcDialog(GLFWwindow* window);

std::optional<std::string> showSaveBedDialog(GLFWwindow* window);
std::optional<std::string> showOpenBedDialog(GLFWwindow* window);

// Whole-session project files (see io/ProjectIO.h) -- everything the
// editor knows that a .src has nowhere to store.
std::optional<std::string> showSaveProjectDialog(GLFWwindow* window);
std::optional<std::string> showOpenProjectDialog(GLFWwindow* window);

// suggestedName pre-fills the save dialog (e.g. the active object's name + ".src").
std::optional<std::string> showSaveSrcDialog(GLFWwindow* window, const std::string& suggestedName);
