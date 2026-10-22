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
#include "peripheral/sd_card.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cstdarg>
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

// False when the byte counter cannot be trusted (see the control in main()).
// A blind instrument reads 0 for everything, which would pass every bound, so
// while it is false every row FAILS.
bool g_instrument_ok = false;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (!g_instrument_ok) {
        cond = false;
        std::printf("  (the /proc/self/io rchar control failed: this row measured nothing)\n");
    }
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
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string detail(const char* what, uint64_t bytes) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s read %llu bytes", what,
                  static_cast<unsigned long long>(bytes));
    return buf;
}

/// `rchar` of this process, or 0 when it cannot be read (the control in
/// main() then fails every row).
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
          ring(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(65536)),
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

void make_emu(Emulator& e, const std::string& card, bool readonly = true) {
    EmulatorConfig c;
    c.type = MachineType::ZXN_ISSUE2;
    c.rewind_buffer_frames = 0;
    c.sd_card_image = card;
    c.sd_card_readonly = readonly;
    e.init(c);
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
                             ("jnext_nodigest_" + jtp::process_id_string())).string();
    std::filesystem::create_directories(dir, ec);
    jtp::set_env("JNEXT_CONFIG_DIR", dir.c_str());

    std::printf("No-whole-card-read tests (GH #396)\n\n");

    // Positive control: read a known 32 MiB of the card and require the
    // counter to have moved by at least that much. Without it an unreadable
    // /proc/self/io (rchar() == 0) would pass every bound below.
    {
        constexpr uint64_t kControl = 32ull * 1024 * 1024;
        std::ifstream f(card, std::ios::binary);
        std::vector<char> buf(1024 * 1024);
        const uint64_t r0 = rchar();
        for (uint64_t got = 0; got < kControl && f; got += buf.size())
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const uint64_t r1 = rchar();
        g_instrument_ok = r0 != 0 && r1 >= r0 + kControl;
        if (!g_instrument_ok)
            std::printf("  CONTROL FAILED: rchar %llu -> %llu after reading %llu bytes\n",
                        static_cast<unsigned long long>(r0),
                        static_cast<unsigned long long>(r1),
                        static_cast<unsigned long long>(kControl));
    }

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

    // ── NODIG-05: the state AFTER a real recording (GH #396 review F2) ──
    //
    // Plan section 4.4 item 3: after a recording, the in-memory generation and
    // the process-wide memo are set. Needs a REAL recording (the 500-frame
    // boot) on a writable reflinked copy of the card, deleted afterwards.
    {
        using Clock = std::filesystem::file_time_type;
        const std::string rec = dir + "/rec.img";
        const std::string cp = "cp --reflink=auto '" + card + "' '" + rec + "'";
        const bool copied = std::system(cp.c_str()) == 0;
        std::filesystem::remove_all(warm_start::cache_dir(), ec);

        // generation > 0 before the recording, from an own write to a scratch
        // card (the generation is process-wide)
        const std::string scratch = dir + "/scratch.img";
        {
            std::ofstream f(scratch, std::ios::binary | std::ios::trunc);
            const std::vector<char> z(64 * 1024, 0);
            f.write(z.data(), static_cast<std::streamsize>(z.size()));
        }
        bool primed = false;
        {
            Emulator p;
            make_emu(p, scratch, false);
            primed = write_sector(p, 1) && SdCardDevice::host_write_generation() > 0;
        }

        Emulator e1;
        make_emu(e1, rec, false);
        LogTap tap(Log::emulator());
        const bool rec1 = copied && e1.ensure_warm_start_state() &&
                          !e1.warm_start_state().empty();
        const int recorded1 = tap.count("recorded a NextZXOS-resident machine");
        const bool again = e1.ensure_warm_start_state();
        const bool reused = again && tap.count("recorded a NextZXOS-resident machine") == 1 &&
                            tap.count("discarding it") == 0 &&
                            tap.count("Not recording") == 0;

        // (ii) an own write with the mtime put back: a fresh Emulator in this
        // process must still re-record (the memo set by the recording)
        std::error_code e;
        const Clock mt0 = std::filesystem::last_write_time(rec, e);
        const bool wrote = write_sector(e1, 2000000);
        std::filesystem::last_write_time(rec, mt0, e);
        const bool mtime_back = !e && std::filesystem::last_write_time(rec, e) == mt0;
        Emulator e2;
        make_emu(e2, rec, false);
        const bool rec2 = e2.ensure_warm_start_state();
        check("NODIG-05",
              "after ONE real recording the in-memory state is reused (no second "
              "boot, no discard even though jnext had written to a card earlier "
              "in the process), and after an own write with the mtime put back "
              "a fresh Emulator re-records",
              copied && primed && rec1 && recorded1 == 1 && reused && wrote &&
                  mtime_back && rec2 &&
                  tap.count("recorded a NextZXOS-resident machine") == 2 &&
                  tap.count("wrote to this SD image since the recording") == 1,
              det("copied=%d primed=%d rec1=%d recorded1=%d reused=%d wrote=%d "
                  "mtime_back=%d rec2=%d recorded=%d memo=%d",
                  copied, primed, rec1, recorded1, reused, wrote, mtime_back, rec2,
                  tap.count("recorded a NextZXOS-resident machine"),
                  tap.count("wrote to this SD image since the recording")));
        std::filesystem::remove(rec, e);
    }

    std::filesystem::remove_all(dir, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
