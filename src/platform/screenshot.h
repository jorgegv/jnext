#pragma once
#include <string>
#include <cstdint>

/// Save an ARGB8888 framebuffer as a PNG file.
///
/// `width` × `height` describe the IN-MEMORY framebuffer (canonical 640×256
/// post-G104). The PNG file is always emitted at `width × (height * 2)`:
/// each input row is written twice to vertically double the output, giving
/// users a square-pixel CRT-faithful 4:3 PNG without post-processing.
///
/// Returns true on success.
bool save_screenshot_png(const std::string& path,
                         const uint32_t* framebuffer,
                         int width, int height);
