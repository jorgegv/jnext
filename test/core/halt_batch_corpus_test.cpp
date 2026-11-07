// GH #452 - bit-identical equivalence of the cycle-batching optimisations over
// every NEX program the project keeps: test/00regression/nex and
// test/nex-nonworking (46 programs; nextipede.tap is a TAP and is out).
//
// Each program is loaded the way `jnext --load X.nex` loads it (the NextZXOS
// warm start, from the SD image the unit harness provisions as a per-run clone,
// opened read-only; never the master), runs the same scripted keys with the
// batching switches off and on, and must agree after every frame on the
// framebuffer, the drained audio and the full save_state() stream
// (batch_ab.h). The oracle is the unbatched path of the same binary.
//
// The programs run at the speed NextZXOS or the program sets (28 MHz for most,
// 14 MHz for four), never 3.5 MHz: the warm start restores NextZXOS's speed and
// --cpu-speed is only a starting value. The 3.5 MHz cases are the synthetic
// fixtures of halt_batch_equiv_test.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "batch_ab.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include "../row_id.h"

#ifndef JNEXT_SOURCE_DIR
#error "JNEXT_SOURCE_DIR must name the repository root (test/CMakeLists.txt)"
#endif

// The corpus is split into HBC_SHARDS executables built from this one source when
// its wall time asks for it; shard HBC_SHARD owns the programs whose index is
// congruent to it. One shard = the whole corpus.
#ifndef HBC_SHARDS
#define HBC_SHARDS 1
#endif
#ifndef HBC_SHARD
#define HBC_SHARD 0
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
};

const Program kPrograms[] = {
    {"HBC-01", "00regression/nex/beanbros.nex"},
    {"HBC-02", "00regression/nex/beast.nex"},
    {"HBC-03", "00regression/nex/blue.nex"},
    {"HBC-04", "00regression/nex/celeste.nex"},
    {"HBC-05", "00regression/nex/celeste2.nex"},
    {"HBC-06", "00regression/nex/contention_test.nex"},
    {"HBC-07", "00regression/nex/copper_demo.nex"},
    {"HBC-08", "00regression/nex/dsl_demo.nex"},
    {"HBC-09", "00regression/nex/dsl_demo_buggy.nex"},
    {"HBC-10", "00regression/nex/floating_bus_test.nex"},
    {"HBC-11", "00regression/nex/joy_uart_demo.nex"},
    {"HBC-12", "00regression/nex/layer2_320x256_test.nex"},
    {"HBC-13", "00regression/nex/layer2_640x256_test.nex"},
    {"HBC-14", "00regression/nex/lores_demo.nex"},
    {"HBC-15", "00regression/nex/magic_bp_demo.nex"},
    {"HBC-16", "00regression/nex/magic_port_demo.nex"},
    {"HBC-17", "00regression/nex/menu.nex"},
    {"HBC-18", "00regression/nex/odemo.nex"},
    {"HBC-19", "00regression/nex/palette_demo.nex"},
    {"HBC-20", "00regression/nex/parallax.nex"},
    {"HBC-21", "00regression/nex/red.nex"},
    {"HBC-22", "00regression/nex/santaspressie.nex"},
    {"HBC-23", "00regression/nex/shift.nex"},
    {"HBC-24", "00regression/nex/show512.nex"},
    {"HBC-25", "00regression/nex/sprite_anchor_test.nex"},
    {"HBC-26", "00regression/nex/sprite_scaling_test.nex"},
    {"HBC-27", "00regression/nex/stencil_test.nex"},
    {"HBC-28", "00regression/nex/test01l2empty.nex"},
    {"HBC-29", "00regression/nex/test02layer2.nex"},
    {"HBC-30", "00regression/nex/test03sprite.nex"},
    {"HBC-31", "00regression/nex/test04tilemap.nex"},
    {"HBC-32", "00regression/nex/test05print.nex"},
    {"HBC-33", "00regression/nex/test06keyb.nex"},
    {"HBC-34", "00regression/nex/test07joystick.nex"},
    {"HBC-35", "00regression/nex/test09videoint.nex"},
    {"HBC-36", "00regression/nex/test10tilemapper.nex"},
    {"HBC-37", "00regression/nex/tilemap_demo.nex"},
    {"HBC-38", "00regression/nex/trainyard-express.nex"},
    {"HBC-39", "nex-nonworking/bloaters.nex"},
    {"HBC-40", "nex-nonworking/hott.nex"},
    {"HBC-41", "nex-nonworking/lom.nex"},
    {"HBC-42", "nex-nonworking/nextoid.nex"},
    {"HBC-43", "nex-nonworking/night-knight-demo.nex"},
    {"HBC-44", "nex-nonworking/revivalsurvival.nex"},
    {"HBC-45", "nex-nonworking/TX-1696.nex"},
    {"HBC-46", "nex-nonworking/warhawk.nex"},
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

} // namespace

int main() {
    std::printf("GH #452 cycle-batching equivalence tests (NEX corpus, shard %d of %d)\n",
                HBC_SHARD, HBC_SHARDS);
    std::printf("===============================================\n\n");

    const std::string root = std::string(JNEXT_SOURCE_DIR) + "/test/";
    int index = 0;
    for (const Program& p : kPrograms) {
        if (index++ % HBC_SHARDS != HBC_SHARD) continue;
        std::string why_a, why_b;
        const batch_ab::Run a = run_program(root + p.path, false, why_a);
        const batch_ab::Run b = run_program(root + p.path, true, why_b);
        std::string diff = batch_ab::first_diff(a, b);
        if (diff.empty() && (!why_a.empty() || !why_b.empty())) diff = why_a + why_b;
        std::printf("  %s %s: halt_batched=%llu copper_skipped=%llu\n", p.id, p.path,
                    static_cast<unsigned long long>(b.halt_batched),
                    static_cast<unsigned long long>(b.copper_skipped));
        check(p.id, std::string(p.path) + ": identical per frame, batching off vs on",
              diff.empty(), diff);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
