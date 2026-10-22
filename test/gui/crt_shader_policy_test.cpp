// ===========================================================================
// OpenGL CRT display filter — pure decision rules (doc/design/CRT-GL-FILTER.md).
//
// No VHDL oracle: this is host display plumbing, not emulated hardware. The
// oracles are (a) crtemu's own public enum, which the persisted style numbers
// must equal, (b) the project's fullscreen rule — the largest integer scale at
// which 640x512 fits, G104 Phase 7 — which the overlay must be placed by so the
// filtered and unfiltered pictures occupy the same rectangle, and (c) OpenGL's
// GL_RGBA/GL_UNSIGNED_BYTE byte order, which is what crtemu uploads.
//
// Runs with no Qt and no GL context, so it is part of the SDL-only set too.
//
// Discriminative — each rule was mutation-tested against src/gui/crt_shader_policy.h:
//   CRT-POL-02  Style::Pc and Style::Lite swapped                 -> fails
//   CRT-POL-03  style_from_int() returns static_cast without range-check -> fails
//   CRT-POL-05..07  fullscreen_scale() using max instead of min   -> fails
//   CRT-POL-10..13  argb_to_abgr() reduced to a plain copy         -> fails
//   CRT-POL-14  the upstream crop expressions put back into crtemu.h -> fails
//
// Run: ./build/test/crt_shader_policy_test
// ===========================================================================

#include "gui/crt_shader_policy.h"
#include "crtemu/crtemu.h"      // declarations only: no CRTEMU_IMPLEMENTATION, no GL
#include "../row_id.h"

#include <climits>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using crt_shader::Style;

bool little_endian() {
    const uint32_t one = 1;
    unsigned char b[4];
    std::memcpy(b, &one, 4);
    return b[0] == 1;
}

}  // namespace

int main() {
    std::printf("OpenGL CRT display filter - policy\n");
    std::printf("=========================================================================\n\n");

    check("CRT-POL-01", "style_from_int maps 0, 1, 2 to Tv, Pc, Lite",
          crt_shader::style_from_int(0) == Style::Tv &&
          crt_shader::style_from_int(1) == Style::Pc &&
          crt_shader::style_from_int(2) == Style::Lite);

    // The overlay hands the value to crtemu_create() as a crtemu_type_t. If the
    // two ever disagree the user picks "PC monitor" and gets the TV.
    check("CRT-POL-02", "the persisted style numbers equal crtemu's own crtemu_type_t",
          static_cast<int>(Style::Tv)   == static_cast<int>(CRTEMU_TYPE_TV) &&
          static_cast<int>(Style::Pc)   == static_cast<int>(CRTEMU_TYPE_PC) &&
          static_cast<int>(Style::Lite) == static_cast<int>(CRTEMU_TYPE_LITE));

    check("CRT-POL-03", "an out-of-range persisted style falls back to the default, never to an invalid enum",
          crt_shader::style_from_int(-1) == crt_shader::STYLE_DEFAULT &&
          crt_shader::style_from_int(crt_shader::STYLE_COUNT) == crt_shader::STYLE_DEFAULT &&
          crt_shader::style_from_int(100) == crt_shader::STYLE_DEFAULT &&
          crt_shader::style_from_int(INT_MIN) == crt_shader::STYLE_DEFAULT &&
          crt_shader::style_from_int(INT_MAX) == crt_shader::STYLE_DEFAULT);

    check("CRT-POL-04", "the default style is the full TV effect and is one of the valid styles",
          crt_shader::STYLE_DEFAULT == Style::Tv && crt_shader::STYLE_COUNT == 3);

    // 1920x1080: width allows 3x, height 1080/512 = 2x -> the height decides.
    check("CRT-POL-05", "fullscreen_scale on 1920x1080 is 2 (height decides)",
          crt_shader::fullscreen_scale(1920, 1080, 640, 512) == 2);
    // 3840x2160: width 6x, height 2160/512 = 4x.
    check("CRT-POL-06", "fullscreen_scale on 3840x2160 is 4",
          crt_shader::fullscreen_scale(3840, 2160, 640, 512) == 4);
    // Width decides here: 1000/640 = 1, 2000/512 = 3.
    check("CRT-POL-07", "fullscreen_scale takes the smaller axis (width-limited 1000x2000 is 1)",
          crt_shader::fullscreen_scale(1000, 2000, 640, 512) == 1);
    check("CRT-POL-08", "fullscreen_scale never goes below 1, even on a screen smaller than the picture",
          crt_shader::fullscreen_scale(320, 200, 640, 512) == 1 &&
          crt_shader::fullscreen_scale(0, 0, 640, 512) == 1 &&
          crt_shader::fullscreen_scale(640, 512, 640, 512) == 1);
    check("CRT-POL-09", "fullscreen_scale with a degenerate picture size is 1, not a division by zero",
          crt_shader::fullscreen_scale(1920, 1080, 0, 512) == 1 &&
          crt_shader::fullscreen_scale(1920, 1080, 640, 0) == 1 &&
          crt_shader::fullscreen_scale(1920, 1080, -640, 512) == 1);

    // 0xAARRGGBB with R=0x11 G=0x22 B=0x33 A=0x80  ->  0xAABBGGRR.
    {
        const uint32_t in = 0x80112233u;
        uint32_t out = 0;
        crt_shader::argb_to_abgr(&in, &out, 1);
        check("CRT-POL-10", "argb_to_abgr swaps the red and blue channels and keeps green and alpha",
              out == 0x80332211u);
    }
    // What OpenGL actually reads: the first four bytes in memory are R, G, B, A.
    {
        const uint32_t in = 0xFF112233u;
        uint32_t out = 0;
        crt_shader::argb_to_abgr(&in, &out, 1);
        unsigned char b[4];
        std::memcpy(b, &out, 4);
        check("CRT-POL-11", "the converted pixel's bytes in memory are R, G, B, A (GL_RGBA/GL_UNSIGNED_BYTE)",
              little_endian() && b[0] == 0x11 && b[1] == 0x22 && b[2] == 0x33 && b[3] == 0xFF);
    }
    // Several distinct pixels, an odd count, and the extremes.
    {
        const uint32_t in[5]  = { 0xFF000000u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF0000FFu, 0x00123456u };
        const uint32_t exp[5] = { 0xFF000000u, 0xFFFFFFFFu, 0xFF0000FFu, 0xFFFF0000u, 0x00563412u };
        uint32_t out[5] = {};
        crt_shader::argb_to_abgr(in, out, 5);
        bool ok = true;
        for (int i = 0; i < 5; ++i) ok = ok && out[i] == exp[i];
        check("CRT-POL-12", "every pixel of a run is converted (black, white, pure red, pure blue, alpha 0)", ok);
    }
    // In place, and an involution; a zero count writes nothing.
    {
        uint32_t px[3] = { 0xFF102030u, 0x80A0B0C0u, 0x01020304u };
        const uint32_t orig[3] = { px[0], px[1], px[2] };
        crt_shader::argb_to_abgr(px, px, 3);
        const bool changed = px[0] != orig[0] && px[1] != orig[1] && px[2] != orig[2];
        crt_shader::argb_to_abgr(px, px, 3);
        const bool identity = px[0] == orig[0] && px[1] == orig[1] && px[2] == orig[2];
        uint32_t guard = 0xDEADBEEFu;
        crt_shader::argb_to_abgr(&guard, &guard, 0);
        check("CRT-POL-13", "argb_to_abgr works in place, applying it twice is the identity, and n == 0 writes nothing",
              changed && identity && guard == 0xDEADBEEFu);
    }

    // The vendored shader: upstream's TV/PC fragment shaders crop the picture (a vertical
    // 0.92 rescale in TV, a 1.156 zoom in PC, a 4% window shrink and a 0.3% shift in both),
    // which cut the top and bottom rows of jnext's 640x256 frame while leaving the sides.
    // Shader pixels cannot be asserted without a GPU, so pin the patch that removes it.
    {
        std::ifstream f(JNEXT_CRTEMU_HEADER);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string h = ss.str();
        auto has = [&](const char* t) { return h.find(t) != std::string::npos; };
        check("CRT-POL-14", "the vendored crtemu carries patch 7: none of upstream's picture crops is in its shaders",
              !h.empty() && has("JNEXT PATCH 7") &&
              !has("tc * vec2(1.025, 0.92) + vec2(-0.0125, 0.04);") &&
              !has("tc * vec2(1.156, 1.156) - vec2( 0.078 + 0.003, 0.078 );") &&
              !has("float scale = 0.04;") &&
              !has("scale/2.0+vec2(0.003, -0.001)"),
              h.empty() ? "header not readable" : "");
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
