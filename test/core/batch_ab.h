// GH #452 - the A/B rig shared by halt_batch_equiv_test and the
// halt_batch_corpus_1..8_test shards.
//
// The cycle-batching optimisations (the HALT repeat batch, the Copper unmet-WAIT
// skip) are claimed BIT-IDENTICAL to the per-slot, per-cycle paths they replace.
// This rig is how the claim is held: build the same machine twice in one
// binary, one with the switches off (the unbatched path, which is today's
// behaviour and is independently held to the VHDL by the other suites and the
// screenshot regression), one with the switches under test, run the same
// scripted input, and compare THREE 64-bit hashes after every frame:
//   video  the ARGB framebuffer,
//   audio  every stereo sample the mixer produced during the frame (and the count),
//   state  the whole Emulator::save_state() stream.
// An A/B proves equivalence, not accuracy; equivalence is the claim made.
#pragma once

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace batch_ab {

// Word-at-a-time: a byte FNV over a 2.3 MB state costs about a second per
// thousand frames, which a 46-program corpus cannot afford.
inline uint64_t hash_bytes(const void* p, size_t n, uint64_t h = 0x9E3779B97F4A7C15ull) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    h ^= h >> 32;
    return h;
}

struct FrameHash {
    uint64_t video = 0;
    uint64_t audio = 0;
    uint64_t state = 0;
    int      samples = 0;      // stereo pairs the mixer produced in the frame
};

struct Run {
    bool                   built = false;
    bool                   alive = true;     // the fixture's own liveness predicate
    std::vector<FrameHash> frames;
    uint64_t               halt_batched = 0;     // Emulator::halt_repeats_batched_for_test()
    uint64_t               copper_skipped = 0;   // Emulator::copper_cycles_skipped_for_test()
    uint64_t               audio_pairs = 0;      // total over the run
};

// Scratch buffers reused across frames and runs.
struct Scratch {
    std::vector<int16_t> samples = std::vector<int16_t>(2 * 32768);
    std::vector<uint8_t> state;
};

inline FrameHash capture(Emulator& emu, Scratch& s) {
    FrameHash h;
    const size_t fb_bytes = sizeof(uint32_t) *
        static_cast<size_t>(emu.get_framebuffer_width()) *
        static_cast<size_t>(emu.get_framebuffer_height());
    h.video = hash_bytes(emu.get_framebuffer(), fb_bytes);

    uint64_t a = 0x1234567;
    for (;;) {
        const int n = emu.mixer().read_samples(s.samples.data(), 32768);
        if (n <= 0) break;
        a = hash_bytes(s.samples.data(), sizeof(int16_t) * 2u * static_cast<size_t>(n), a);
        h.samples += n;
    }
    h.audio = hash_bytes(&h.samples, sizeof h.samples, a);

    StateWriter measure;
    emu.save_state(measure);
    if (s.state.size() < measure.position()) s.state.resize(measure.position());
    StateWriter w(s.state.data(), s.state.size());
    emu.save_state(w);
    h.state = hash_bytes(s.state.data(), w.position());
    return h;
}

// What a scenario is: how to build the machine, what to do before the first
// frame and between frames, how many frames, and the fixture's own liveness.
struct Scenario {
    const char* name = "";
    int         frames = 300;
    // Fills the EmulatorConfig; default = a bare Next, no SD image.
    std::function<void(EmulatorConfig&)>   config;
    // Runs once after init(), before the first frame.
    std::function<void(Emulator&)>         setup;
    // Runs before frame `f` (0-based).
    std::function<void(Emulator&, int)>    before_frame;
    // Runs after frame `f`'s run_frame() and before its hashes are taken: the seam the
    // rig's own controls use to disturb exactly one component of exactly one frame.
    std::function<void(Emulator&, int)>    after_frame;
    // A predicate on the final machine: "the fixture did what it exists to do".
    // A fixture that silently stops doing its job is a named failure.
    std::function<bool(Emulator&)>         alive;
};

inline Run run(const Scenario& sc, bool halt_batch, bool copper_skip, Scratch& scratch) {
    Run r;
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (sc.config) sc.config(cfg);
    if (!emu->init(cfg)) return r;
    emu->set_cycle_batching_for_test(halt_batch, copper_skip);
    if (sc.setup) sc.setup(*emu);
    r.built = true;
    r.frames.reserve(static_cast<size_t>(sc.frames));
    for (int f = 0; f < sc.frames; ++f) {
        if (sc.before_frame) sc.before_frame(*emu, f);
        emu->run_frame();
        if (sc.after_frame) sc.after_frame(*emu, f);
        r.frames.push_back(capture(*emu, scratch));
        r.audio_pairs += static_cast<uint64_t>(r.frames.back().samples);
    }
    r.alive = sc.alive ? sc.alive(*emu) : true;
    r.halt_batched = emu->halt_repeats_batched_for_test();
    r.copper_skipped = emu->copper_cycles_skipped_for_test();
    return r;
}

// "" when every frame's three hashes agree, else the first frame and component.
inline std::string first_diff(const Run& a, const Run& b) {
    if (!a.built || !b.built) return "machine not built";
    if (a.frames.size() != b.frames.size()) return "frame count differs";
    for (size_t i = 0; i < a.frames.size(); ++i) {
        const FrameHash& x = a.frames[i];
        const FrameHash& y = b.frames[i];
        const char* what = x.state != y.state ? "state"
                         : x.video != y.video ? "video"
                         : x.audio != y.audio ? "audio" : nullptr;
        if (what) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "frame %zu %s", i, what);
            return buf;
        }
    }
    return {};
}

} // namespace batch_ab
