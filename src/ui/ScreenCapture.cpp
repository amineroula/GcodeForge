#include "ui/ScreenCapture.h"

#include <GL/glew.h>

#include <cstdint>
#include <cstdio>
#include <vector>

bool writeScreenshotBmp(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    // 24bpp rows are padded to a multiple of 4 bytes -- standard BMP
    // requirement, not optional.
    int rowSize = (width * 3 + 3) & ~3;
    int imageSize = rowSize * height;

    std::vector<uint8_t> pixels(static_cast<size_t>(imageSize));
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    const int fileHeaderSize = 14;
    const int infoHeaderSize = 40;
    const int dataOffset = fileHeaderSize + infoHeaderSize;
    const int fileSize = dataOffset + imageSize;

    uint8_t fileHeader[fileHeaderSize] = {0};
    fileHeader[0] = 'B'; fileHeader[1] = 'M';
    fileHeader[2] = static_cast<uint8_t>(fileSize & 0xFF);
    fileHeader[3] = static_cast<uint8_t>((fileSize >> 8) & 0xFF);
    fileHeader[4] = static_cast<uint8_t>((fileSize >> 16) & 0xFF);
    fileHeader[5] = static_cast<uint8_t>((fileSize >> 24) & 0xFF);
    fileHeader[10] = static_cast<uint8_t>(dataOffset & 0xFF);
    fileHeader[11] = static_cast<uint8_t>((dataOffset >> 8) & 0xFF);

    uint8_t infoHeader[infoHeaderSize] = {0};
    infoHeader[0] = infoHeaderSize;
    infoHeader[4] = static_cast<uint8_t>(width & 0xFF);
    infoHeader[5] = static_cast<uint8_t>((width >> 8) & 0xFF);
    infoHeader[6] = static_cast<uint8_t>((width >> 16) & 0xFF);
    infoHeader[7] = static_cast<uint8_t>((width >> 24) & 0xFF);
    infoHeader[8] = static_cast<uint8_t>(height & 0xFF);
    infoHeader[9] = static_cast<uint8_t>((height >> 8) & 0xFF);
    infoHeader[10] = static_cast<uint8_t>((height >> 16) & 0xFF);
    infoHeader[11] = static_cast<uint8_t>((height >> 24) & 0xFF);
    infoHeader[12] = 1;  // planes
    infoHeader[14] = 24; // bits per pixel
    infoHeader[20] = static_cast<uint8_t>(imageSize & 0xFF);
    infoHeader[21] = static_cast<uint8_t>((imageSize >> 8) & 0xFF);
    infoHeader[22] = static_cast<uint8_t>((imageSize >> 16) & 0xFF);
    infoHeader[23] = static_cast<uint8_t>((imageSize >> 24) & 0xFF);

    std::fwrite(fileHeader, 1, fileHeaderSize, file);
    std::fwrite(infoHeader, 1, infoHeaderSize, file);
    std::fwrite(pixels.data(), 1, pixels.size(), file);
    std::fclose(file);
    return true;
}
