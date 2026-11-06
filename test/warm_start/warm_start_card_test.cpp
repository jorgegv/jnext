// GH #464 - the warm start against a REAL NextZXOS card: the --sdcard-sha256
// key and the stepped recording.
//
// No VHDL oracle: the recording is a jnext-internal mechanism (the warm_start
// rows say so too). The oracle is the plan: in digest mode the cache header
// holds the caller's SHA-256 at offset 40, the mtime field is 0 and the size
// field is the card's; prepare_warm_start(n) advances exactly n frames per
// call and its result equals the one-shot recording; and a recording taken
// after jnext has written to a card is used but not cached.
//
//   WSCARD-01  digest mode: ensure() records; header digest at 40, mtime 0, size
//   WSCARD-02  a stepped recording (7 frames per call) equals the one-shot one
//   WSCARD-03  after a stepped Ready, ensure() is the in-memory hit: no new recording
//   WSCARD-04  after an own write the recording is used but not cached
//
// The card is the run's SD clone (JNEXT_TEST_SD_IMAGE), mounted READ-ONLY, so
// the shared clone is never written and its mtime never moves. The RTC is
// pinned and the config directory is private.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/warm_start_cache.h"
#include "peripheral/sd_card.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string det(const char* f, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string card_path() {
    if (const char* e = std::getenv("JNEXT_TEST_SD_IMAGE")) return e;
    const char* home = std::getenv("HOME");
    if (!(home && *home)) home = std::getenv("USERPROFILE");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

struct LogTap {
    std::shared_ptr<spdlog::logger>                    log;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring;
    spdlog::level::level_enum                          saved;
    explicit LogTap(std::shared_ptr<spdlog::logger> l)
        : log(std::move(l)),
          ring(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(4096)),
          saved(log->level()) {
        log->sinks().push_back(ring);
        log->set_level(spdlog::level::info);
    }
    ~LogTap() { log->set_level(saved); log->sinks().pop_back(); }
    int count(const char* needle) const {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find(needle) != std::string::npos) ++n;
        return n;
    }
};

const std::string kHex(64, 'a');
constexpr const char* kRecorded = "recorded a NextZXOS-resident machine";

EmulatorConfig card_config(const std::string& card, bool regenerate) {
    EmulatorConfig c;
    c.type = MachineType::ZXN_ISSUE2;
    c.rewind_buffer_frames = 0;
    c.sd_card_image = card;
    c.sd_card_readonly = true;
    c.sd_card_sha256 = kHex;
    c.sd_card_sha256_for = card;
    c.warm_start_regenerate = regenerate;
    c.rtc_fixed = true;
    c.rtc_fixed_tm.tm_year = 126;   // 2026
    c.rtc_fixed_tm.tm_mon = 0;
    c.rtc_fixed_tm.tm_mday = 1;
    c.rtc_fixed_tm.tm_hour = 12;
    return c;
}

std::vector<uint8_t> read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

uint64_t le64(const std::vector<uint8_t>& b, size_t off) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[off + static_cast<size_t>(i)];
    return v;
}

/// One CMD24 of `sector` through the emulator's own card device.
bool write_sector(Emulator& e, uint32_t sector) {
    SdCardDevice& sd = e.sd_card();
    sd.reset();
    auto cmd = [&](uint8_t c, uint32_t arg) {
        sd.receive(0x40 | c);
        sd.receive(static_cast<uint8_t>(arg >> 24));
        sd.receive(static_cast<uint8_t>(arg >> 16));
        sd.receive(static_cast<uint8_t>(arg >> 8));
        sd.receive(static_cast<uint8_t>(arg));
        sd.receive(0x95);
        uint8_t r = 0xFF;
        for (int i = 0; i < 16 && r == 0xFF; ++i) r = sd.send();
        return r;
    };
    cmd(0, 0); cmd(8, 0x1AA); cmd(55, 0); cmd(41, 0x40000000); cmd(58, 0);
    const uint8_t r1 = cmd(24, sector);
    sd.receive(0xFE);
    for (int i = 0; i < 512; ++i) sd.receive(0);
    sd.receive(0xFF);
    sd.receive(0xFF);
    bool accepted = false;
    for (int i = 0; i < 32; ++i) {
        if (sd.send() == 0x05) { accepted = true; break; }
    }
    sd.deselect();
    return r1 == 0x00 && accepted;
}

}  // namespace

int main() {
    const std::string card = card_path();
    std::error_code ec;
    if (!std::filesystem::exists(card, ec)) {
        std::printf("FATAL: SD image %s not found\n", card.c_str());
        return 2;
    }
    const std::string dir = (std::filesystem::temp_directory_path() /
                             ("jnext_wscard_" + jtp::process_id_string())).string();
    std::filesystem::create_directories(dir, ec);
    jtp::set_env("JNEXT_CONFIG_DIR", dir.c_str());
    const std::string cache = warm_start::cache_path(static_cast<uint8_t>(MachineType::ZXN_ISSUE2));

    std::printf("Warm start against a real card (GH #464)\n\n");

    std::vector<uint8_t> state1;
    // ── WSCARD-01 ────────────────────────────────────────────────────
    {
        Emulator e;
        e.init(card_config(card, false));
        const bool ok = e.ensure_warm_start_state();
        state1 = e.warm_start_state();
        const auto raw = read_file(cache);
        bool digest_ok = raw.size() >= warm_start::kHeaderBytes;
        for (size_t i = 0; digest_ok && i < 32; ++i) digest_ok = raw[40 + i] == 0xAA;
        const uint64_t size = std::filesystem::file_size(card, ec);
        check("WSCARD-01",
              "in digest mode a recording is stored with the digest at header offset 40, "
              "mtime 0 and the card's size",
              ok && !state1.empty() && digest_ok && raw.size() >= warm_start::kHeaderBytes &&
                  le64(raw, 32) == 0 && le64(raw, 24) == size,
              det("ok=%d digest=%d size_field=%llu card=%llu", ok, digest_ok,
                  raw.size() >= 32 ? (unsigned long long)le64(raw, 24) : 0ull,
                  (unsigned long long)size));
    }
    // ── WSCARD-02 / 03 ───────────────────────────────────────────────
    {
        Emulator e;
        e.init(card_config(card, true));
        LogTap tap(Log::emulator());
        uint32_t calls = 0;
        bool steps_ok = true;
        Emulator::WarmStartProgress p;
        do {
            p = e.prepare_warm_start(7);
            ++calls;
            if (p.status == Emulator::WarmStartProgress::Status::Recording)
                steps_ok = steps_ok && p.frames_done == calls * 7;
        } while (p.status == Emulator::WarmStartProgress::Status::Recording && calls < 1000);
        const bool ready = p.status == Emulator::WarmStartProgress::Status::Ready;
        check("WSCARD-02",
              "a recording stepped 7 frames per call advances exactly 7 each time, ends at "
              "500 frames after 72 calls, and is byte-identical to the one-shot recording",
              ready && steps_ok && calls == 72 && p.frames_done == 500 && !state1.empty() &&
                  e.warm_start_state() == state1,
              det("ready=%d steps_ok=%d calls=%u done=%u same=%d", ready, steps_ok, calls,
                  p.frames_done, e.warm_start_state() == state1));

        const int before = tap.count(kRecorded);
        const bool again = e.ensure_warm_start_state();
        check("WSCARD-03",
              "after a stepped Ready, ensure_warm_start_state() is the in-memory hit: true "
              "with no new recording",
              ready && before == 1 && again && tap.count(kRecorded) == before,
              det("before=%d after=%d again=%d", before, tap.count(kRecorded), again));
    }
    // ── WSCARD-04 ────────────────────────────────────────────────────
    {
        // The card was first mounted (WSCARD-01) before this write, so its
        // digest is stale from here on.
        const std::string scratch = dir + "/scratch.img";
        {
            std::ofstream f(scratch, std::ios::binary | std::ios::trunc);
            const std::vector<char> z(64 * 1024, 0);
            f.write(z.data(), static_cast<std::streamsize>(z.size()));
        }
        bool wrote = false;
        {
            Emulator w;
            EmulatorConfig sc;
            sc.type = MachineType::ZXN_ISSUE2;
            sc.rewind_buffer_frames = 0;
            sc.sd_card_image = scratch;
            w.init(sc);
            wrote = write_sector(w, 1);
        }
        std::filesystem::remove_all(warm_start::cache_dir(), ec);
        Emulator e;
        e.init(card_config(card, false));
        LogTap tap(Log::emulator());
        const bool ok = e.ensure_warm_start_state();
        check("WSCARD-04",
              "after an own write to a card the recording is used but not cached: ensure() "
              "is true, a recording is made, the 'not cached' line is logged and no cache "
              "file exists",
              wrote && ok && !e.warm_start_state().empty() && tap.count(kRecorded) == 1 &&
                  tap.count("recorded but not cached") == 1 && !std::filesystem::exists(cache, ec),
              det("wrote=%d ok=%d recorded=%d notcached=%d cache_exists=%d", wrote, ok,
                  tap.count(kRecorded), tap.count("recorded but not cached"),
                  std::filesystem::exists(cache, ec)));
    }

    std::filesystem::remove_all(dir, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
