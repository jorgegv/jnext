// ===========================================================================
// Screenshot output: formats, auto-naming and write failures (GH #18, #19).
//
// NO VHDL ORACLE, deliberately. What this suite covers is host-side file
// behaviour — which extension means which format, what an auto-generated name
// looks like, whether a byte count on disk is right, and whether a write that
// did not happen is reported as one. The FPGA core writes no files at all. The
// hardware half of `.SCR` — which bank and which address window the dump comes
// from — is `ula_test`'s S18 group, cited to zxula.vhd / zxnext.vhd there.
//
// WHY THE FAILURE ROWS ARE HERE. A screenshot that silently did not appear is
// the exact defect GH #19 is written against ("the status bar confirms the
// path written, so the file is never silently lost"), and the PNG writer used
// to carry a documented hole of that shape: the whole image fits inside
// glibc's stdio buffer, so nothing is written until fclose(), whose result was
// not checked — on a full disk it printed "saved" and exited 0 over a
// truncated file. SH-30..33 pin both writers against both halves of that:
// an unwritable path (caught at fopen) and a failing FLUSH.
//
// The flush half uses /dev/full, the POSIX character device that accepts an
// open and fails every write with ENOSPC. It is the only way to make the
// disk-full path deterministic without filling a real filesystem. Where it is
// not available the row falls back to the unwritable-path stimulus, so it
// still asserts something true rather than passing vacuously — and says which
// stimulus it used in its detail string. Unit tests run on Linux (the dev host
// and CI's fedora:44 container), where it is always present.
// ===========================================================================
#include "platform/screenshot.h"
#include "video/ula.h"
#include "memory/ram.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

// A scratch directory of this process's own, removed at the end. Not
// QTemporaryDir: this suite deliberately links neither Qt nor SDL.
std::string g_tmp;

std::string make_tmp_dir() {
    char tmpl[] = "/tmp/jnext-shot-test-XXXXXX";
    const char* d = mkdtemp(tmpl);
    return d ? std::string(d) : std::string();
}

void rm_rf(const std::string& dir) {
    // Shallow by construction: nothing here creates sub-directories except
    // auto_screenshot_path's own target, which is removed by name first.
    std::string cmd = "rm -rf '" + dir + "'";
    if (system(cmd.c_str()) != 0) { /* best effort */ }
}

bool exists(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0; }

long file_size(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? static_cast<long>(st.st_size) : -1L;
}

std::vector<uint8_t> read_file(const std::string& p) {
    std::vector<uint8_t> out;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return out;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return out;
}

// True when /dev/full behaves as POSIX says (open succeeds, write fails).
// Checked rather than assumed, so the two flush rows can say which stimulus
// they actually used.
bool have_dev_full() {
    FILE* f = fopen("/dev/full", "wb");
    if (!f) return false;
    fclose(f);
    struct stat st{};
    return ::stat("/dev/full", &st) == 0 && S_ISCHR(st.st_mode);
}

// ── Group SH-0x — the extension IS the format ────────────────────────

void test_format_from_extension() {
    struct Case { const char* path; ScreenshotFormat want; const char* why; };
    const Case cases[] = {
        { "shot.png",            ScreenshotFormat::Png, "plain .png" },
        { "shot.scr",            ScreenshotFormat::Scr, "plain .scr" },
        { "SHOT.SCR",            ScreenshotFormat::Scr, "upper case" },
        { "SHOT.PNG",            ScreenshotFormat::Png, "upper case" },
        { "shot.ScR",            ScreenshotFormat::Scr, "mixed case" },
        { "shot",                ScreenshotFormat::Png, "no extension at all" },
        { "shot.jpg",            ScreenshotFormat::Png, "unknown extension" },
        { "shot.scr.png",        ScreenshotFormat::Png, "only the LAST extension counts" },
        { "shot.png.scr",        ScreenshotFormat::Scr, "only the LAST extension counts" },
        { "/tmp/v1.0/shot",      ScreenshotFormat::Png, "a dot in a DIRECTORY is not an extension" },
        { "/tmp/v1.scr/shot",    ScreenshotFormat::Png, "a .scr DIRECTORY is not a .scr file" },
        { "",                    ScreenshotFormat::Png, "empty path" },
    };
    std::vector<std::string> bad;
    for (const Case& c : cases)
        if (screenshot_format_for_path(c.path) != c.want)
            bad.push_back(std::string(c.path) + " (" + c.why + ")");
    check("SH-01",
          "the extension selects the format, case-insensitively, last one only",
          bad.empty(),
          bad.empty() ? std::string() : fmt("%zu wrong: %s", bad.size(), bad[0].c_str()));

    check("SH-02",
          "PNG is the fallback, so every pre-.SCR caller keeps its old format",
          screenshot_format_for_path("shot.bmp") == ScreenshotFormat::Png
              && screenshot_format_for_path("shot.") == ScreenshotFormat::Png,
          "an unrecognised or empty extension must never change format");

    check("SH-03",
          "screenshot_format_ext() round-trips both formats",
          std::string(screenshot_format_ext(ScreenshotFormat::Png)) == ".png"
              && std::string(screenshot_format_ext(ScreenshotFormat::Scr)) == ".scr"
              && screenshot_format_for_path(
                     std::string("x") + screenshot_format_ext(ScreenshotFormat::Scr))
                     == ScreenshotFormat::Scr,
          "the extension a format names must be the one that selects it back");

    // SH-04 — the LENGTH boundary: a name that is nothing but the extension.
    //
    // Not synthetic. The Save Screenshot dialog only appends an extension when
    // the user typed none, so typing just ".scr" produces exactly this path,
    // and `--delayed-screenshot .scr` is a legal command line. It is the case
    // that separates `path.size() < n` from `path.size() <= n` in
    // ends_with_ci(), and nothing else in this suite reaches it: every other
    // path here is strictly longer than its extension. The shorter-than-the-
    // extension half (".sc") is here for the same reason — it is the only
    // input that needs the guard at all.
    //
    // SCOPE, measured rather than assumed: this row catches `< n` weakened to
    // `<= n` (it fails on ".scr"). It does NOT catch the guard being DELETED —
    // `path.size() - n` then underflows a size_t and the indexing that follows
    // is undefined behaviour, which happened to keep every row green when it
    // was tried. No assertion can be relied on to catch UB, so that is stated
    // here rather than papered over with a row that passes for accidental
    // reasons.
    {
        std::vector<std::string> bad;
        struct Case { const char* path; ScreenshotFormat want; };
        const Case cases[] = {
            { ".scr",            ScreenshotFormat::Scr },  // length == extension
            { ".png",            ScreenshotFormat::Png },
            { ".SCR",            ScreenshotFormat::Scr },
            { "/tmp/shots/.scr", ScreenshotFormat::Scr },  // basename == extension
            { ".sc",             ScreenshotFormat::Png },  // shorter than extension
            { "scr",             ScreenshotFormat::Png },  // no dot at all
        };
        for (const Case& c : cases)
            if (screenshot_format_for_path(c.path) != c.want) bad.push_back(c.path);
        check("SH-04",
              "a name that is exactly its extension still selects that format",
              bad.empty(),
              bad.empty() ? std::string() : fmt("%zu wrong: %s", bad.size(), bad[0].c_str()));
    }
}

// ── Group SH-1x — auto-generated names (GH #19) ──────────────────────

void test_auto_naming() {
    // A fixed local time, so the name is pinned rather than being whatever the
    // clock said when the suite ran. The expected stamp is computed with
    // localtime_r from the same time_t — not hardcoded — because the file name
    // is local time and the suite must not depend on the host's zone.
    const std::time_t when = 1750000000;  // 2025-06-15 in UTC
    std::tm tm{};
    localtime_r(&when, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

    const std::string dir = g_tmp + "/auto";
    const std::string expect_png = dir + "/jnext-" + stamp + ".png";
    const std::string expect_scr = dir + "/jnext-" + stamp + ".scr";

    const std::string p1 = auto_screenshot_path(dir, ScreenshotFormat::Png, when);
    check("SH-10",
          "the auto name is <dir>/jnext-YYYYMMDD-HHMMSS.png",
          p1 == expect_png, fmt("got '%s' want '%s'", p1.c_str(), expect_png.c_str()));

    check("SH-11",
          "the directory is created on demand, so the first quick capture works",
          exists(dir), fmt("'%s' was not created", dir.c_str()));

    check("SH-12",
          "the configured format picks the extension, since there is no filename to read one from",
          auto_screenshot_path(dir, ScreenshotFormat::Scr, when) == expect_scr,
          fmt("got '%s'", auto_screenshot_path(dir, ScreenshotFormat::Scr, when).c_str()));

    // Nothing has been written yet, so the same second returns the same name —
    // the counter is driven by files on disk, not by a call count.
    check("SH-13",
          "an unused name is returned unsuffixed, twice running",
          auto_screenshot_path(dir, ScreenshotFormat::Png, when) == expect_png,
          "the suffix must come from a collision, not from being asked twice");

    // Now create the collisions.
    FILE* f = fopen(expect_png.c_str(), "wb"); if (f) fclose(f);
    const std::string p2 = auto_screenshot_path(dir, ScreenshotFormat::Png, when);
    check("SH-14",
          "a taken name gains -02, so two captures in one second cannot overwrite",
          p2 == dir + "/jnext-" + stamp + "-02.png",
          fmt("got '%s'", p2.c_str()));

    f = fopen(p2.c_str(), "wb"); if (f) fclose(f);
    const std::string p3 = auto_screenshot_path(dir, ScreenshotFormat::Png, when);
    check("SH-15",
          "the counter keeps climbing: -03 once -02 is taken too",
          p3 == dir + "/jnext-" + stamp + "-03.png",
          fmt("got '%s'", p3.c_str()));

    // The .scr name is independent of the .png ones: the extension is part of
    // the collision test, so switching format does not inherit a counter.
    check("SH-16",
          "the collision test includes the extension, so .scr is not shifted by taken .png names",
          auto_screenshot_path(dir, ScreenshotFormat::Scr, when) == expect_scr,
          fmt("got '%s'", auto_screenshot_path(dir, ScreenshotFormat::Scr, when).c_str()));

    // A directory that cannot be created is reported, not silently worked
    // around: /dev/null is a file, so nothing can be made underneath it.
    check("SH-17",
          "an uncreatable directory yields an empty path instead of writing elsewhere",
          auto_screenshot_path("/dev/null/nope", ScreenshotFormat::Png, when).empty(),
          "a quick capture with nowhere to go must be reported, not redirected");

    check("SH-18",
          "default_quick_screenshot_dir() honours JNEXT_CONFIG_DIR, as the config file does",
          default_quick_screenshot_dir() == g_tmp + "/cfg/screenshots",
          default_quick_screenshot_dir());

    // A directory typed into Preferences often ends in a separator; the path
    // is shown to the user, so it must not come back with a doubled one.
    check("SH-19",
          "a trailing separator on the directory does not double the one in the path",
          auto_screenshot_path(dir + "/", ScreenshotFormat::Scr, when) == expect_scr,
          auto_screenshot_path(dir + "/", ScreenshotFormat::Scr, when));
}

// ── Group SH-2x — the .SCR body reaches the file verbatim ────────────

void test_scr_write() {
    std::vector<uint8_t> body(6912);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);

    const std::string path = g_tmp + "/dump.scr";
    const bool ok = save_screenshot_scr(path, body);
    check("SH-20",
          "save_screenshot_scr() reports success and writes exactly the bytes it was given",
          ok && file_size(path) == 6912 && read_file(path) == body,
          fmt("ok=%d size=%ld", ok ? 1 : 0, file_size(path)));

    // The 12288-byte Timex body goes through unchanged too — the writer must
    // not know or care which layout it holds.
    std::vector<uint8_t> timex(12288, 0x5A);
    timex.front() = 0x11; timex.back() = 0x99;
    const std::string tpath = g_tmp + "/timex.scr";
    check("SH-21",
          "a 12288-byte Timex body is written whole, not truncated to 6912",
          save_screenshot_scr(tpath, timex) && file_size(tpath) == 12288
              && read_file(tpath) == timex,
          fmt("size=%ld", file_size(tpath)));

    // End-to-end through the dispatcher, with a real Ula over real RAM: the
    // .SCR route must read the ULA memory and ignore the framebuffer, and the
    // PNG route must ignore the ULA. One framebuffer serves both.
    Ram ram;
    Ula ula;
    ula.set_ram(&ram);
    ula.reset();
    ram.write(10u * 8192u + 0x0000, 0x3C);   // bank 5, first pixel byte
    ram.write(10u * 8192u + 0x1800, 0x47);   // bank 5, first attribute byte
    std::vector<uint32_t> fb(640 * 256, 0xFF102030u);

    const std::string disp_scr = g_tmp + "/disp.scr";
    const bool scr_ok = save_screenshot(disp_scr, fb.data(), 640, 256, ula);
    const std::vector<uint8_t> got = read_file(disp_scr);
    check("SH-22",
          "save_screenshot() sends a .scr target to the ULA dump, not to libpng",
          scr_ok && got.size() == 6912 && got[0] == 0x3C && got[6144] == 0x47,
          fmt("ok=%d size=%zu [0]=%02X [6144]=%02X", scr_ok ? 1 : 0, got.size(),
              got.empty() ? 0 : got[0], got.size() > 6144 ? got[6144] : 0));

    const std::string disp_png = g_tmp + "/disp.png";
    const bool png_ok = save_screenshot(disp_png, fb.data(), 640, 256, ula);
    const std::vector<uint8_t> png = read_file(disp_png);
    check("SH-23",
          "save_screenshot() sends everything else to the PNG writer",
          png_ok && png.size() > 8
              && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G',
          fmt("ok=%d size=%zu", png_ok ? 1 : 0, png.size()));

    // GH #104 — the PNG is vertically doubled at export. Pinned here because
    // this suite is now the one place that reads a written PNG back: the IHDR
    // height field lives at offset 20..23, big-endian.
    const uint32_t png_h = png.size() > 23
        ? (uint32_t(png[20]) << 24 | uint32_t(png[21]) << 16
           | uint32_t(png[22]) << 8 | uint32_t(png[23])) : 0;
    const uint32_t png_w = png.size() > 19
        ? (uint32_t(png[16]) << 24 | uint32_t(png[17]) << 16
           | uint32_t(png[18]) << 8 | uint32_t(png[19])) : 0;
    check("SH-24",
          "a 640x256 framebuffer is written as a 640x512 PNG (square pixels, G104)",
          png_w == 640 && png_h == 512, fmt("IHDR says %ux%u", png_w, png_h));
}

// ── Group SH-3x — a write that did not happen is reported as one ─────

void test_write_failures() {
    const std::vector<uint8_t> body(6912, 0xAA);
    std::vector<uint32_t> fb(640 * 256, 0xFF000000u);

    const std::string nodir_scr = g_tmp + "/no-such-dir/x.scr";
    const std::string nodir_png = g_tmp + "/no-such-dir/x.png";
    check("SH-30",
          "save_screenshot_scr() fails on an unwritable path and leaves no file",
          !save_screenshot_scr(nodir_scr, body) && !exists(nodir_scr),
          "an unopenable target must be a failure, not a silent no-op");
    check("SH-31",
          "save_screenshot_png() fails on an unwritable path and leaves no file",
          !save_screenshot_png(nodir_png, fb.data(), 640, 256) && !exists(nodir_png),
          "an unopenable target must be a failure, not a silent no-op");

    // The flush half. /dev/full opens fine and fails every write with ENOSPC,
    // which is precisely the disk-full shape: a few KB never leave the stdio
    // buffer, so the only write(2) — and the only error — is at fclose().
    const bool dev_full = have_dev_full();
    const char* how = dev_full ? "stimulus: /dev/full (ENOSPC)"
                               : "stimulus: unwritable path (/dev/full unavailable)";

    // 6912 bytes is larger than glibc's 4 KiB stdio buffer, so fwrite() itself
    // reaches the device and fails: the SHORT-WRITE arm.
    const std::string target = dev_full ? std::string("/dev/full") : nodir_scr;
    check("SH-32",
          "save_screenshot_scr() reports a short write rather than a truncated file",
          !save_screenshot_scr(target, body), how);

    // A body that fits inside the stdio buffer never reaches the device until
    // the close: the FLUSH arm, and the one the PNG writer's documented hole
    // was in. Two rows because they are two different branches — a writer that
    // checked fwrite and ignored fclose still passes SH-32.
    check("SH-34",
          "save_screenshot_scr() reports a failing flush, not just a failing write",
          !save_screenshot_scr(target, std::vector<uint8_t>(64, 0x5A)), how);

    const std::string ptarget = dev_full ? std::string("/dev/full") : nodir_png;
    check("SH-33",
          "save_screenshot_png() reports a failed flush rather than a truncated PNG",
          !save_screenshot_png(ptarget, fb.data(), 640, 256), how);
}

} // namespace

int main() {
    std::printf("Screenshot formats, auto-naming and write failures (GH #18, #19)\n");
    std::printf("================================================================\n\n");

    g_tmp = make_tmp_dir();
    if (g_tmp.empty()) {
        std::printf("  FAIL SH-00: cannot create a scratch directory\n");
        std::printf("Total:    1  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    // Redirect the default quick-screenshot directory away from the real
    // ~/.jnext for the whole run (SH-18 asserts the redirection itself).
    setenv("JNEXT_CONFIG_DIR", (g_tmp + "/cfg").c_str(), 1);

    test_format_from_extension();
    test_auto_naming();
    test_scr_write();
    test_write_failures();

    rm_rf(g_tmp);

    std::printf("\n================================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
