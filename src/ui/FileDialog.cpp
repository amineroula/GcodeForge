#include "ui/FileDialog.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>

namespace {
std::string wideBufferToUtf8(const wchar_t* buffer) {
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(sizeNeeded) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}
} // namespace
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
    return wideBufferToUtf8(fileBuffer);
#else
    (void)window;
    return std::nullopt;
#endif
}

std::optional<std::string> showSaveBedDialog(GLFWwindow* window) {
#ifdef _WIN32
    wchar_t fileBuffer[MAX_PATH] = L"bed.bed";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFilter = L"Bed settings\0*.bed\0All files\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Save bed settings";
    ofn.lpstrDefExt = L"bed";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return wideBufferToUtf8(fileBuffer);
#else
    (void)window;
    return std::nullopt;
#endif
}

std::optional<std::string> showOpenBedDialog(GLFWwindow* window) {
#ifdef _WIN32
    wchar_t fileBuffer[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFilter = L"Bed settings\0*.bed\0All files\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open bed settings";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return wideBufferToUtf8(fileBuffer);
#else
    (void)window;
    return std::nullopt;
#endif
}
