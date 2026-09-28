#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <vector>

class Ula;

/// Screenshot output format. Chosen from the output filename's extension —
/// there is no separate format flag anywhere in jnext, so a name is always
/// enough to know what will be written (GH #18).
enum class ScreenshotFormat {
    Png,   ///< The composited 640x256 framebuffer, doubled to a 640x512 PNG.
    Scr,   ///< The raw ULA screen memory (`.SCR`): 6912 bytes, or 12288 in a
           ///< Timex hi-colour / hi-res mode. See Ula::screen_dump().
};

/// The format `path`'s extension asks for: `.scr` (any case) selects Scr,
/// everything else — including no extension at all — selects Png, which is
/// the format every caller defaulted to before `.SCR` existed.
ScreenshotFormat screenshot_format_for_path(const std::string& path);

/// The canonical lowercase extension for `fmt`, leading dot included.
const char* screenshot_format_ext(ScreenshotFormat fmt);

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

/// Save a `.SCR` body (from Ula::screen_dump()) verbatim.
///
/// Returns true only when every byte reached the file AND the stream closed
/// cleanly — a short write or a failed flush is a failure, not a smaller file
/// reported as saved.
bool save_screenshot_scr(const std::string& path,
                         const std::vector<uint8_t>& bytes);

/// Save the screen in whatever format `path`'s extension names. The one place
/// that dispatches, so the three frontends and the GUI menu cannot drift on
/// which extensions mean what.
///
/// `framebuffer`/`width`/`height` are used by the PNG route; `ula` by the
/// `.SCR` route, which ignores the framebuffer entirely (a `.SCR` records the
/// ULA layer's memory, never the composited picture).
bool save_screenshot(const std::string& path,
                     const uint32_t* framebuffer, int width, int height,
                     const Ula& ula);

/// GH #19 — a collision-free auto-generated screenshot path inside `dir`:
/// `<dir>/jnext-YYYYMMDD-HHMMSS<ext>`, and `-NN` appended (from 2) while that
/// name already exists, so a burst of captures inside one second cannot
/// overwrite each other. `when` is a local `time_t`; it is a parameter so the
/// naming can be pinned by a test instead of being whatever the clock said.
///
/// Returns an empty string if `dir` cannot be created, or if a free name
/// cannot be found — the caller reports that rather than writing somewhere
/// the user did not ask for.
std::string auto_screenshot_path(const std::string& dir,
                                 ScreenshotFormat fmt,
                                 std::time_t when);

/// The default quick-screenshot directory when the user has configured none:
/// `$JNEXT_CONFIG_DIR/screenshots`, else `~/.jnext/screenshots`. The same
/// JNEXT_CONFIG_DIR the GUI config honours, so an automated run redirects
/// both with one variable.
std::string default_quick_screenshot_dir();
