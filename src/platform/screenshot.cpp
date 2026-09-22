#include "screenshot.h"
#include "core/log.h"
#include "video/ula.h"
#include <png.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <vector>
#include <cstdio>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

// Does `path` END with `suffix`, ignoring case?
//
// Deliberately an end-of-string test rather than "parse out the extension":
// the only question anyone asks of a screenshot path is which of two
// extensions it names, and matching the tail answers it without inventing an
// extension for a path like "/tmp/v1.scr/shot", where the only dot is in a
// DIRECTORY name.
bool ends_with_ci(const std::string& path, const char* suffix)
{
    const std::size_t n = std::strlen(suffix);
    if (path.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(
            std::tolower(static_cast<unsigned char>(path[path.size() - n + i])));
        if (a != suffix[i]) return false;
    }
    return true;
}

bool file_exists(const std::string& path)
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// mkdir -p for the quick-screenshot directory. Reported by the caller when it
// fails: a capture that silently went nowhere is exactly what GH #19 exists to
// stop.
bool make_directory_path(const std::string& dir)
{
    if (dir.empty()) return false;
    std::string acc;
    std::size_t i = 0;
    if (dir[0] == '/') { acc = "/"; i = 1; }
    while (i <= dir.size()) {
        if (i == dir.size() || dir[i] == '/') {
            if (!acc.empty() && acc != "/") {
                // _mkdir on MinGW takes no mode argument; the Windows build
                // compiles this file too (ENABLE_TESTS is off there, the
                // frontends are not).
#ifdef _WIN32
                const int rc = ::_mkdir(acc.c_str());
#else
                const int rc = ::mkdir(acc.c_str(), 0755);
#endif
                if (rc != 0 && errno != EEXIST) {
                    Log::platform()->error("screenshot: cannot create directory '{}': {}",
                                           acc, std::strerror(errno));
                    return false;
                }
            }
            if (i == dir.size()) break;
            if (!acc.empty() && acc.back() != '/') acc += '/';
        } else {
            acc += dir[i];
        }
        ++i;
    }
    return true;
}

} // namespace

ScreenshotFormat screenshot_format_for_path(const std::string& path)
{
    return ends_with_ci(path, ".scr") ? ScreenshotFormat::Scr
                                      : ScreenshotFormat::Png;
}

const char* screenshot_format_ext(ScreenshotFormat fmt)
{
    return fmt == ScreenshotFormat::Scr ? ".scr" : ".png";
}

bool save_screenshot_png(const std::string& path,
                         const uint32_t* framebuffer,
                         int width, int height) {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        // Name the path AND the reason: "cannot open" alone leaves the user
        // guessing between a missing directory, a permission problem and a
        // read-only mount.
        Log::platform()->error("screenshot: cannot open '{}' for writing: {}",
                               path, std::strerror(errno));
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

    // libpng longjmps here when a write fails MID-STREAM, i.e. only when stdio
    // actually issues a write(2) while rows are still being emitted.
    //
    // It does NOT cover the usual disk-full case, because a 640x512 screenshot
    // compresses to a few KB and so never leaves glibc's stdio buffer until
    // the final flush. That flush is the fclose() at the end of this function,
    // whose result IS checked — see the comment there.
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        Log::platform()->error("screenshot: PNG write error on '{}': {}",
                               path, std::strerror(errno));
        return false;
    }

    png_init_io(png, fp);
    // PNG file dimensions: width × (height * 2). The input framebuffer is
    // 640×256; the PNG is 640×512 — each in-memory row written twice for
    // square-pixel CRT-faithful 4:3 geometry (G104 Phase 7).
    png_set_IHDR(png, info, width, height * 2, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Convert ARGB8888 to RGB888 row by row, emitting each row twice
    // (vertical 2× doubling at PNG-export time).
    std::vector<uint8_t> row(width * 3);
    for (int y = 0; y < height; ++y) {
        const uint32_t* src = framebuffer + y * width;
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = src[x];
            row[x * 3 + 0] = (pixel >> 16) & 0xFF;  // R
            row[x * 3 + 1] = (pixel >>  8) & 0xFF;  // G
            row[x * 3 + 2] = (pixel >>  0) & 0xFF;  // B
        }
        // Each input row contributes two consecutive output rows.
        png_write_row(png, row.data());
        png_write_row(png, row.data());
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);

    // fclose() is where a 640x512 PNG's data actually reaches the disk: it
    // compresses to a few KB, so it fits entirely inside glibc's stdio buffer
    // and no write(2) happens until this flush. Ignoring the result was the
    // gap the comment on setjmp() above used to record: on a full disk the
    // file was truncated and "saved" was printed anyway, exit 0. Checked now,
    // so the only silent-truncation route left is closed (GH #18/#19).
    if (fclose(fp) != 0) {
        Log::platform()->error("screenshot: failed to flush '{}': {}",
                               path, std::strerror(errno));
        return false;
    }

    Log::platform()->info("screenshot saved to '{}'", path);
    return true;
}

bool save_screenshot_scr(const std::string& path, const std::vector<uint8_t>& bytes)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        Log::platform()->error("screenshot: cannot open '{}' for writing: {}",
                               path, std::strerror(errno));
        return false;
    }
    const size_t written = bytes.empty() ? 0
                                         : fwrite(bytes.data(), 1, bytes.size(), fp);
    if (written != bytes.size()) {
        Log::platform()->error("screenshot: short write to '{}' ({} of {} bytes): {}",
                               path, written, bytes.size(), std::strerror(errno));
        fclose(fp);
        return false;
    }
    // Same reason as the PNG route above: 6912/12288 bytes fit in the stdio
    // buffer, so the write(2) — and any ENOSPC — happens here.
    if (fclose(fp) != 0) {
        Log::platform()->error("screenshot: failed to flush '{}': {}",
                               path, std::strerror(errno));
        return false;
    }
    Log::platform()->info("screenshot saved to '{}' ({} bytes, .SCR)", path, bytes.size());
    return true;
}

bool save_screenshot(const std::string& path,
                     const uint32_t* framebuffer, int width, int height,
                     const Ula& ula)
{
    if (screenshot_format_for_path(path) == ScreenshotFormat::Scr)
        return save_screenshot_scr(path, ula.screen_dump());
    return save_screenshot_png(path, framebuffer, width, height);
}

std::string default_quick_screenshot_dir()
{
    // JNEXT_CONFIG_DIR is the same override AppConfig::default_config_path()
    // honours, so a test run redirects the config file and the quick-capture
    // directory together and never touches the developer's real ~/.jnext.
    const char* cfg = std::getenv("JNEXT_CONFIG_DIR");
    if (cfg && *cfg) return std::string(cfg) + "/screenshots";
    // HOME first, USERPROFILE second: the same order that makes this resolve
    // to the directory QDir::homePath() reports on both platforms, so the
    // quick captures land beside ~/.jnext/jnext.conf and not somewhere else.
    for (const char* var : {"HOME", "USERPROFILE"}) {
        const char* home = std::getenv(var);
        if (home && *home) return std::string(home) + "/.jnext/screenshots";
    }
    return ".jnext/screenshots";
}

std::string auto_screenshot_path(const std::string& dir, ScreenshotFormat fmt,
                                 std::time_t when)
{
    if (!make_directory_path(dir)) return {};

    // A directory typed into Preferences may carry a trailing separator; the
    // path is shown to the user in the status bar, so join it cleanly rather
    // than emitting "shots//jnext-...". Root stays "/".
    std::string base_dir = dir;
    while (base_dir.size() > 1 && base_dir.back() == '/') base_dir.pop_back();

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &when);
#else
    localtime_r(&when, &tm);
#endif
    char stamp[32];
    if (std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm) == 0) return {};

    const std::string base = (base_dir == "/" ? std::string("/")
                                            : base_dir + "/") + "jnext-" + stamp;
    const std::string ext  = screenshot_format_ext(fmt);

    // Bare name first, then -02, -03, ... Bounded rather than unbounded: a
    // loop that cannot terminate is worse than a reported failure, and 100
    // captures inside one second is not a use case, it is a stuck caller.
    std::string candidate = base + ext;
    for (int n = 2; n <= 99 && file_exists(candidate); ++n) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "-%02d", n);
        candidate = base + suffix + ext;
    }
    if (file_exists(candidate)) {
        Log::platform()->error(
            "screenshot: no free auto-generated name for '{}{}' (99 taken)",
            base, ext);
        return {};
    }
    return candidate;
}
