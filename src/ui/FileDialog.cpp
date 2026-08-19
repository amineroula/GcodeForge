#include "ui/FileDialog.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>
#endif

std::optional<std::string> showOpenSrcDialog(GLFWwindow* window) {
#ifdef _WIN32
    wchar_t fileBuffer[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFilter = L"KUKA SRC / G-code\0*.src;*.gcode;*.nc\0All files\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open toolpath file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return std::nullopt;

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, fileBuffer, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(sizeNeeded) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, fileBuffer, -1, result.data(), sizeNeeded, nullptr, nullptr);
    return result;
#else
    (void)window;
    return std::nullopt;
#endif
}
