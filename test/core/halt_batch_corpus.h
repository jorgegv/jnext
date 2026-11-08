// GH #452 - bit-identical equivalence of the cycle-batching optimisations over
// every NEX program the project keeps: test/00regression/nex and
// test/nex-nonworking (46 programs; nextipede.tap is a TAP and is out).
//
// Each program is loaded the way `jnext --load X.nex` loads it (the NextZXOS
// warm start). The SD image is $JNEXT_TEST_SD_IMAGE, which the unit harness sets to
// its per-run clone; run standalone, the suite falls back to the master under
// ~/.jnext/sdcard. Either way it is opened read-only. Each program runs the same scripted keys with the
// batching switches off and on, and must agree after every frame on the
// framebuffer, the drained audio and the full save_state() stream
// (batch_ab.h). The oracle is the unbatched path of the same binary.
//
// The programs run at the speed NextZXOS or the program sets (28 MHz for most,
// 14 MHz for four), never 3.5 MHz: the warm start restores NextZXOS's speed and
// --cpu-speed is only a starting value. The 3.5 MHz cases are the synthetic
// fixtures of halt_batch_equiv_test.

#pragma once

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "batch_ab.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>
#include "../row_id.h"

#ifndef JNEXT_SOURCE_DIR
#error "JNEXT_SOURCE_DIR must name the repository root (test/CMakeLists.txt)"
#endif


namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const std::string& desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc.c_str());
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string sd_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

// Scripted input: SPACE on frames 50-54, ENTER on 100-104, Q on 150-159.
constexpr int kFrames = 160;

void script_keys(Emulator& e, int f) {
    auto key = [&](SDL_Scancode sc, int from, int to) {
        if (f == from) e.keyboard().set_key(sc, true);
        if (f == to)   e.keyboard().set_key(sc, false);
    };
    key(SDL_SCANCODE_SPACE, 50, 55);
    key(SDL_SCANCODE_RETURN, 100, 105);
    key(SDL_SCANCODE_Q, 150, 160);
}


struct Program {
    const char* id;       // a literal row ID
    const char* path;     // relative to the repository's test/ directory
    bool copper = false;  // runs a Copper program: the Copper skip must engage (GH #452 stage 2)
};


batch_ab::Scratch g_scratch;

batch_ab::Run run_program(const std::string& path, bool batch, std::string& why) {
    batch_ab::Scenario sc;
    sc.name = "corpus";
    sc.frames = kFrames;
    sc.config = [&](EmulatorConfig& c) {
        c.sd_card_image = sd_image();
        c.sd_card_readonly = true;
        c.load_file = path;
        c.rtc_fixed = true;
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 0; tm.tm_mday = 2;
        tm.tm_hour = 3; tm.tm_min = 4; tm.tm_sec = 5;
        c.rtc_fixed_tm = tm;
    };
    sc.setup = [&](Emulator& e) {
        if (!e.load_nex(path)) why = "load_nex failed";
    };
    sc.before_frame = script_keys;
    return batch_ab::run(sc, batch, batch, g_scratch);
}

int run_corpus(const Program* programs, int count, const char* label) {
    std::printf("GH #452 cycle-batching equivalence tests (NEX corpus, %s)\n", label);
    std::printf("===============================================\n\n");

    const std::string root = std::string(JNEXT_SOURCE_DIR) + "/test/";
    for (int i = 0; i < count; ++i) {
        const Program& p = programs[i];
        const auto t0 = std::chrono::steady_clock::now();
        std::string why_a, why_b;
        const batch_ab::Run a = run_program(root + p.path, false, why_a);
        const batch_ab::Run b = run_program(root + p.path, true, why_b);
        std::string diff = batch_ab::first_diff(a, b);
        if (diff.empty() && (!why_a.empty() || !why_b.empty())) diff = why_a + why_b;
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %s %s: halt_batched=%llu copper_skipped=%llu (%.1f s)\n", p.id, p.path,
                    static_cast<unsigned long long>(b.halt_batched),
                    static_cast<unsigned long long>(b.copper_skipped), secs);
        // The row also names the programs the optimisation must engage in: one that stops
        // engaging (an optimisation silently disabled) cannot pass the corpus.
        if (diff.empty() && p.copper && b.copper_skipped == 0) diff = "the Copper skip never engaged";
        check(p.id, std::string(p.path) + ": identical per frame, batching off vs on" +
                    (p.copper ? ", Copper skip engaged" : ""),
              diff.empty(), diff);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}

} // namespace
