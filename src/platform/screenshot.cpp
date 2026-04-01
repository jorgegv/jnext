#include "screenshot.h"
#include "core/log.h"
#include <png.h>
#include <vector>
#include <cstdio>

bool save_screenshot_png(const std::string& path,
                         const uint32_t* framebuffer,
                         int width, int height) {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        Log::platform()->error("screenshot: cannot open '{}'", path);
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        Log::platform()->error("screenshot: PNG write error");
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Convert ARGB8888 to RGB888 row by row.
    std::vector<uint8_t> row(width * 3);
    for (int y = 0; y < height; ++y) {
        const uint32_t* src = framebuffer + y * width;
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = src[x];
            row[x * 3 + 0] = (pixel >> 16) & 0xFF;  // R
            row[x * 3 + 1] = (pixel >>  8) & 0xFF;  // G
            row[x * 3 + 2] = (pixel >>  0) & 0xFF;  // B
        }
        png_write_row(png, row.data());
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    Log::platform()->info("screenshot saved to '{}'", path);
    return true;
}
