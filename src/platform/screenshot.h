#pragma once
#include <string>
#include <cstdint>

/// Save an ARGB8888 framebuffer as a PNG file.
/// Returns true on success.
bool save_screenshot_png(const std::string& path,
                         const uint32_t* framebuffer,
                         int width, int height);
