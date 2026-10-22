// GH #396 - Quick Save / Quick Load no longer read the whole SD card.
//
// The stall was a whole-card SHA-256 on the UI thread (0.6-1.2 s per GB). The
// owner removed it (2026-10-22). These rows measure the MECHANISM, not a wall
// clock: Linux `rchar` in /proc/self/io is the number of bytes this process has
// passed through read() syscalls (page-cache hits included). Under the old code
// each operation below read >= 1 GiB of the 1 GB card; the bound is 16 MiB,
// under 2% of it, which leaves room for the .jns itself and the Tier-1 sectors.
// The suite is `# os: linux` because /proc/self/io exists only there.
//
//   NODIG-01  save_jns reads no whole card, writes an identity, no content_stamp
//   NODIG-02  probe_jns_file reads no whole card and accepts the file
//   NODIG-03  load_jns reads no whole card, restores, and warns about no card
//   NODIG-04  a warm-start cache hit reads no whole card and is restored
//
// The card is the run's SD clone (JNEXT_TEST_SD_IMAGE), mounted read-only.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/jns_snapshot.h"
#include "core/log.h"
#include "core/saveable.h"
#include "core/warm_start_cache.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

std::string detail(const char* what, uint64_t bytes) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s read %llu bytes", what,
                  static_cast<unsigned long long>(bytes));
    return buf;
}

/// `rchar` of this process, or 0 when unreadable.
uint64_t rchar() {
    std::ifstream f("/proc/self/io");
    std::string key;
    uint64_t v = 0;
    while (f >> key >> v) {
        if (key == "rchar:") return v;
    }
    return 0;
}

constexpr uint64_t kBound = 16ull * 1024 * 1024;

std::string card_path() {
    if (const char* e = std::getenv("JNEXT_TEST_SD_IMAGE")) return e;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") +
           "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

struct LogTap {
    std::shared_ptr<spdlog::logger>                    log;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring;
    spdlog::level::level_enum                          saved;
    explicit LogTap(std::shared_ptr<spdlog::logger> l)
        : log(std::move(l)),
          ring(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256)),
          saved(log->level()) {
        log->sinks().push_back(ring);
        log->set_level(spdlog::level::trace);
    }
    ~LogTap() { log->set_level(saved); log->sinks().pop_back(); }
    int count(const char* needle) const {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find(needle) != std::string::npos) ++n;
        return n;
    }
};

void make_emu(Emulator& e, const std::string& card) {
    EmulatorConfig c;
    c.type = MachineType::ZXN_ISSUE2;
    c.rewind_buffer_frames = 0;
    c.sd_card_image = card;
    c.sd_card_readonly = true;
    e.init(c);
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
                             ("jnext_nodigest_" + jtp::process_id_string())).string();
    std::filesystem::create_directories(dir, ec);
    jtp::set_env("JNEXT_CONFIG_DIR", dir.c_str());

    std::printf("No-whole-card-read tests (GH #396)\n\n");

    // ── NODIG-01: save_jns ───────────────────────────────────────────
    std::vector<uint8_t> jns;
    {
        Emulator e;
        make_emu(e, card);
        jnext::JnsSaveOptions opt;
        opt.uncompressed = true;     // so the manifest text is greppable
        jnext::JnsLoadReport rep;
        std::string why;
        const uint64_t r0 = rchar();
        const bool ok = e.save_jns(opt, jns, rep, why);
        const uint64_t d = rchar() - r0;
        const std::string text(jns.begin(), jns.end());
        check("NODIG-01",
              "save_jns on a mounted 1 GB card reads under 16 MiB, records the "
              "Tier-1 identity, and writes no content_stamp",
              ok && d < kBound && text.find("fat32_volume_id") != std::string::npos &&
                  text.find("content_stamp") == std::string::npos,
              detail("save_jns", d) + " " + why);
    }
    const std::string path = dir + "/a.jns";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(jns.data()),
                static_cast<std::streamsize>(jns.size()));
    }

    // ── NODIG-02: probe_jns_file ─────────────────────────────────────
    {
        Emulator e;
        make_emu(e, card);
        std::string why;
        const uint64_t r0 = rchar();
        const bool ok = e.probe_jns_file(path, why);
        const uint64_t d = rchar() - r0;
        check("NODIG-02",
              "probe_jns_file reads under 16 MiB and accepts the file",
              ok && d < kBound, detail("probe_jns_file", d) + " " + why);
    }

    // ── NODIG-03: load_jns ───────────────────────────────────────────
    {
        Emulator e;
        make_emu(e, card);
        jnext::JnsLoadOptions lopt;
        jnext::JnsLoadReport rep;
        std::string why;
        const uint64_t r0 = rchar();
        const bool ok = e.load_jns(jns.data(), jns.size(), lopt, rep, why);
        const uint64_t d = rchar() - r0;
        const bool card_warning = std::any_of(
            rep.warnings.begin(), rep.warnings.end(), [](const std::string& w) {
                return w.find("SD card") != std::string::npos;
            });
        check("NODIG-03",
              "load_jns reads under 16 MiB, restores, and raises no SD card "
              "warning",
              ok && d < kBound && !card_warning,
              detail("load_jns", d) + " " + why);
    }

    // ── NODIG-04: a warm-start hit ───────────────────────────────────
    {
        Emulator probe;
        make_emu(probe, card);
        StateWriter measure;
        probe.save_state(measure);
        std::vector<uint8_t> st(measure.position());
        StateWriter w(st.data(), st.size());
        probe.save_state(w);

        warm_start::Identity id;
        id.machine_type = static_cast<uint8_t>(MachineType::ZXN_ISSUE2);
        id.plain_bytes  = st.size();
        id.sd_image_bytes = std::filesystem::file_size(card, ec);
        id.sd_image_mtime = static_cast<int64_t>(
            std::filesystem::last_write_time(card, ec).time_since_epoch().count());
        std::string swhy;
        const bool stored = warm_start::store(id, st, swhy);

        Emulator e;
        make_emu(e, card);
        LogTap tap(Log::emulator());
        const uint64_t r0 = rchar();
        const bool hit = e.ensure_warm_start_state();
        const uint64_t d = rchar() - r0;
        check("NODIG-04",
              "a warm-start cache hit reads under 16 MiB and is restored, not "
              "re-recorded",
              stored && hit && d < kBound &&
                  tap.count("restored a recorded NextZXOS machine") == 1 &&
                  tap.count("Not recording") == 0,
              detail("ensure_warm_start_state", d) + " " + swhy);
    }

    std::filesystem::remove_all(dir, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
