#pragma once

// The decisions behind the OpenGL CRT display filter (doc/design/CRT-GL-FILTER.md).
//
// Pure: no Qt, no OpenGL, no I/O — so every rule here is pinned row by row in
// test/gui/crt_shader_policy_test.cpp without a GPU, a display or a context.
// The widget that uses them (gui/crt_gl_overlay.cpp) can only be exercised on
// a machine with OpenGL; these rules are the part that must be right everywhere.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace crt_shader {

/// Which crtemu preset is drawn. The numbers are persisted (app_config) and
/// shown in Preferences, and match crtemu_type_t (TV=0, PC=1, LITE=2) so the
/// overlay can hand the value straight through — pinned by CRT-POL-02.
enum class Style : int {
    Tv   = 0,   ///< full effect: curvature, bloom, scanlines, shadow mask, noise
    Pc   = 1,   ///< flatter monitor: scanlines and mask, little curvature
    Lite = 2,   ///< cheapest: scanlines only
};

inline constexpr int STYLE_COUNT   = 3;
inline constexpr Style STYLE_DEFAULT = Style::Tv;

/// Persisted value -> style. Anything out of range (a hand-edited or newer
/// config) yields the default rather than an invalid enum that crtemu_create()
/// would index a shader table with.
constexpr Style style_from_int(int v) {
    return (v >= 0 && v < STYLE_COUNT) ? static_cast<Style>(v) : STYLE_DEFAULT;
}

/// The largest integer scale at which NATIVE_W x DISPLAY_H fits a physical
/// pw x ph, never below 1. This is the fullscreen rule EmulatorWidget::prescale()
/// has always used (G104 Phase 7: an integer scale keeps the pixels square);
/// the GL overlay is placed by the SAME function, so the filtered picture and
/// the unfiltered one occupy the identical rectangle.
constexpr int fullscreen_scale(int pw, int ph, int native_w, int display_h) {
    if (native_w <= 0 || display_h <= 0) return 1;
    return std::max(1, std::min(pw / native_w, ph / display_h));
}

/// Convert one ARGB32 framebuffer (what the emulator produces: 0xAARRGGBB as a
/// native-endian word) into the word crtemu uploads as GL_RGBA/GL_UNSIGNED_BYTE
/// (R first in memory, i.e. 0xAABBGGRR on a little-endian host). Without this the
/// red and blue channels arrive swapped. `dst` may not alias `src` partially;
/// full aliasing (in place) is fine.
inline void argb_to_abgr(const uint32_t* src, uint32_t* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t p = src[i];
        dst[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
    }
}

}  // namespace crt_shader
