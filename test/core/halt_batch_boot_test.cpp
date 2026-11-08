// GH #452 - the NextZXOS COLD boot, batching off vs on, per frame.
//
// halt_batch_corpus_*_test load every program through the warm start, which a separate machine
// RECORDS (Emulator::record_warm_start_state(), default switches) and every corpus run then
// restores; so no corpus run ever executes the boot itself with the switches under test. This
// suite does: nextboot.rom through to NextZXOS on the SD image (the unit harness's per-run clone,
// $JNEXT_TEST_SD_IMAGE; standalone, the master under ~/.jnext/sdcard, opened read-only), the RTC
// pinned, the framebuffer and the audio hashed after every frame and the whole save_state() stream
// every 4th frame and at the last. The oracle is the unbatched path of the same binary. A boot has
// every kind of HALT the corpus lacks: firmware loops, the esxDOS and DivMMC paths, the boot ROM
// overlay, the supervisor's interrupt handling.
//
// It is a suite of its own because its wall time is the boot's (about 50 s solo).

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "batch_ab.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include "../row_id.h"

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

std::string sd_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

} // namespace

int main() {
    std::printf("GH #452 cycle-batching equivalence test (NextZXOS cold boot)\n");
    std::printf("===============================================\n\n");

    batch_ab::Scenario sc;
    sc.name = "cold-boot";
    sc.frames = 600;
    sc.state_every = 4;
    sc.config = [](EmulatorConfig& c) {
        c.sd_card_image = sd_image();
        c.sd_card_readonly = true;
        c.rtc_fixed = true;
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 0; tm.tm_mday = 2;
        tm.tm_hour = 3; tm.tm_min = 4; tm.tm_sec = 5;
        c.rtc_fixed_tm = tm;
    };
    batch_ab::Scratch scratch;
    const batch_ab::Run a = batch_ab::run(sc, false, false, scratch);
    const batch_ab::Run b = batch_ab::run(sc, true, true, scratch);
    std::printf("  halt_batched=%llu copper_skipped=%llu audio_pairs=%llu\n",
                static_cast<unsigned long long>(b.halt_batched),
                static_cast<unsigned long long>(b.copper_skipped),
                static_cast<unsigned long long>(b.audio_pairs));

    // HBB-01: the cold boot is identical per frame with the batching switches off and on.
    const std::string d = batch_ab::first_diff(a, b);
    check("HBB-01", "NextZXOS cold boot, 600 frames, batching off vs on: identical video and audio per frame, identical state every 4th frame and at the end, and the OFF run really off",
          d.empty() && a.copper_skipped == 0 && a.halt_batched == 0,
          "diff=" + d + " off: copper_skipped=" + std::to_string(a.copper_skipped) +
          " halt_batched=" + std::to_string(a.halt_batched));

    // HBB-02: the boot reaches something worth comparing (it draws: the screen is not all one
    // colour by the end) and the optimisation engaged in it at least once.
    const bool drew = b.frames.size() > 100 && b.frames[100].video != b.frames.back().video;
    check("HBB-02", "the cold boot's picture changes between frame 100 and the last (the boot ran), and the batching engaged in the 'on' run",
          drew && (b.halt_batched > 0 || b.copper_skipped > 0),
          "halt_batched=" + std::to_string(b.halt_batched) + " copper_skipped=" +
          std::to_string(b.copper_skipped));

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
