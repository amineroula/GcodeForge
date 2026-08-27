#pragma once

#include <string>

// Dumps the current OpenGL framebuffer to a 24bpp BMP file -- a raw
// hand-rolled writer (BITMAPFILEHEADER + BITMAPINFOHEADER, no
// compression) rather than a PNG/stb_image_write dependency, since this
// exists purely so a UI change can be visually verified without a human
// launching the app and taking a screenshot manually (see main.cpp's
// GCODEFORGE_SCREENSHOT_FILE env var). glReadPixels already returns
// bottom-up row order, which is exactly what BMP wants, so no row-flip
// is needed. Call right after the frame's draw calls, before
// glfwSwapBuffers (the framebuffer still holds what was just drawn).
bool writeScreenshotBmp(const std::string& path, int width, int height);
