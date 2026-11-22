// GH #496 - the issue's own scenario: the NextZXOS Guide viewer, batch off vs on, per frame.
//
// A cold boot on the SD image ($JNEXT_TEST_SD_IMAGE, the unit harness's per-run clone; standalone, the
// test master under ~/.jnext/sdcard/test-suite, opened read-only), SPACE at the welcome page, G at the menu (the Guide
// viewer), and 200 frames in the viewer. There the machine waits for the raster line in a DI loop of
// seven instructions that re-selects NR 0x1E every pass (about 97 % of the viewer's T-states): the poll
// batch must engage in it, the picture, the audio and the whole state must agree with the unbatched
// run after every frame, and the CPU must be seen inside that loop. The image and everything made
// from it stay out of the repository: the machine is built at run time.
#include "batch_ab.h"
#include "core/emulator.h"
#include "core/emulator_config.h"


#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include "../row_id.h"
#include "../test_sd_image.h"

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
    return test_sd_image_path();
}

// The script: SPACE at the welcome page, G at the menu it opens.
constexpr int kSpace = 1300, kGuide = 1360, kFrames = 1560;

void keys(Emulator& e, int f) {
    auto key = [&](SDL_Scancode sc, int from, int to) {
        if (f == from) e.keyboard().set_key(sc, true);
        if (f == to)   e.keyboard().set_key(sc, false);
    };
    key(SDL_SCANCODE_SPACE, kSpace, kSpace + 5);
    key(SDL_SCANCODE_G, kGuide, kGuide + 5);
}

// The viewer's wait: 0x386B..0x3884 of the dot command, which runs from DivMMC RAM.
bool in_viewer_loop(Emulator& e) {
    const uint16_t pc = e.cpu().pc();
    return pc >= 0x386B && pc <= 0x3884;
}

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch: the NextZXOS Guide viewer\n");
    std::printf("===============================================\n\n");

    batch_ab::Scenario sc;
    sc.name = "guide-viewer";
    sc.frames = kFrames;
    sc.state_every = 20;
    sc.config = [](EmulatorConfig& c) {
        c.sd_card_image = sd_image();
        c.sd_card_readonly = true;
        c.rtc_fixed = true;
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 0; tm.tm_mday = 2;
        tm.tm_hour = 3; tm.tm_min = 4; tm.tm_sec = 5;
        c.rtc_fixed_tm = tm;
    };
    sc.before_frame = keys;
    // Sampled over the last frames: the CPU is in the viewer's wait loop most of the time.
    auto in_loop = std::make_shared<int>(0);
    sc.after_frame = [in_loop](Emulator& e, int f) {
        if (f == 0) *in_loop = 0;
        if (f >= kFrames - 20 && in_viewer_loop(e)) ++*in_loop;
    };
    batch_ab::Scratch scratch;
    // The poll batch alone (the HALT batch and the Copper skip stay as they are in both runs).
    const batch_ab::Run a = batch_ab::run(sc, true, true, scratch, 0);
    const int in_a = *in_loop;
    const batch_ab::Run b = batch_ab::run(sc, true, true, scratch, 1);
    const int in_b = *in_loop;
    std::printf("  poll_passes_batched=%llu (off run %llu); in the viewer loop at the end of %d/20 frames (off), %d/20 (on)\n",
                static_cast<unsigned long long>(b.poll_batched), static_cast<unsigned long long>(a.poll_batched),
                in_a, in_b);

    const std::string d = batch_ab::first_diff(a, b);
    check("RPB-40", "NextZXOS cold boot, SPACE at the welcome page, G at the menu, 200 frames in the Guide viewer, poll batch off vs on: identical video and audio per frame, identical state every 20th frame and at the end; the batch engaged and the CPU is in the viewer's wait loop",
          d.empty() && a.poll_batched == 0 && b.poll_batched > 0 && in_a >= 10 && in_b >= 10,
          "diff=" + d + " poll_batched off=" + std::to_string(a.poll_batched) + " on=" + std::to_string(b.poll_batched) +
          " in_loop off=" + std::to_string(in_a) + " on=" + std::to_string(in_b));

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
