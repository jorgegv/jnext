// Automatic tape play/stop tests (GH #395): TAPA-01..16, moved out of
// tap_loader_test (GH #547 stage 5) with their IDs, fixtures and assertions
// unchanged, so the two processes are each shorter under load.
//
// No hardware counterpart (no Spectrum has tape motor control): a host deck
// feature. The oracle of each row is cited at the row.
//
// Run: ./build/test/tap_loader_autoplay_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/tap_loader.h"
#include "core/tape_poll_detector.h"
#include "ld_trap_rig.h"
#include "tap_loader_common.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main() {
    std::printf("TAP automatic play/stop tests\n");
    std::printf("===============================================\n\n");

    make_root("jnext-tap-loader-autoplay-test-");

    // ---- Automatic tape play/stop (GH #395) ------------------------------
    // No hardware counterpart (no Spectrum has tape motor control): a host
    // deck feature. Oracle: the design in the plan (B2): the deck stops only
    // at a gap boundary when no one polls the ULA port, resumes at the next
    // block's first pilot pulse when polling starts, and a program that keeps
    // polling never sees a difference. Fixtures: our own synthetic bytes and
    // hand-written Z80 loops.
    {
        // TAPA-01..05: the detector itself.
        {
            TapePollDetector d; uint64_t t = 1000; bool early = false;
            for (int i = 0; i < 31; ++i) { d.on_read(t); early = early || d.polling(t); t += 59; }
            const bool at31 = d.polling(t - 59);
            d.on_read(t);
            check("TAPA-01", "31 reads 59 T apart are not polling, the 32nd is",
                  !early && !at31 && d.polling(t));
        }
        {
            TapePollDetector a, b; uint64_t t = 5000;
            for (int i = 0; i < 40; ++i) { a.on_read(t); b.on_read(t); t += 10; }
            a.on_read(t - 10 + 1000);      // exactly G after the last read: the run continues
            b.on_read(t - 10 + 1001);      // G + 1: a new run of 1
            check("TAPA-02", "a gap of exactly 1000 T continues the run, 1001 T restarts it",
                  a.polling(t - 10 + 1000) && !b.polling(t - 10 + 1001));
        }
        {
            TapePollDetector d; bool ever = false; uint64_t t = 0;
            for (int frame = 0; frame < 200; ++frame) {
                for (int i = 0; i < 8; ++i) { d.on_read(t + i * 59); ever = ever || d.polling(t + i * 59); }
                ever = ever || d.polling(t + 8 * 59 + 100);
                t += 69888;
            }
            check("TAPA-03", "the ROM keyboard scan (bursts of 8 reads 59 T apart, once a frame) is never polling",
                  !ever);
        }
        {
            TapePollDetector d; uint64_t t = 100;
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const uint64_t last = t - 20;
            check("TAPA-04", "a run of 40 reads is not polling once the last read is more than 1000 T old",
                  d.polling(last) && d.polling(last + 1000) && !d.polling(last + 1001));
        }
        {
            TapePollDetector d; uint64_t t = 90000;
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const bool was = d.polling(t - 20);
            d.on_read(50000);                  // time went backwards (a state load)
            const bool back = d.polling(50000) || d.polling(60000);
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const bool again = d.polling(t - 20);
            d.reset();
            check("TAPA-05", "time going backwards and reset() both end a run; no wrap to polling",
                  was && !back && again && !d.polling(t - 20));
        }
    }

    {
        using namespace ldrig;
        // Delay without reading the port (M outer rounds of ~1.7 M T), then an
        // edge loop. BC = 0xFFFE keeps the port address fixed.
        auto delay_then_poll = [](uint16_t m) {
            return Bytes{0xF3, 0x11, uint8_t(m & 0xFF), uint8_t(m >> 8),
                         0x21, 0x00, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,
                         0x1B, 0x7A, 0xB3, 0x20, 0xF3,
                         0xED, 0x78, 0x18, 0xFC};
        };
        const Bytes poll_only = {0xF3, 0xED, 0x78, 0x18, 0xFC};
        const Bytes blk = tap_block(0xFF, {1, 2, 3, 4, 5});
        const std::string two = write_file("auto2.tap", concat({blk, tap_block(0xFF, {6, 7, 8, 9})}));
        // A header block then a data block: their pilots differ (8063 / 3223 pulses).
        const std::string hdr_two = write_file("autoh.tap",
                                               concat({tap_block(0x00, Bytes(17, 0x20)), tap_block(0xFF, {6, 7, 8, 9})}));

        auto mk = [&](const std::string& tap, bool fast, bool autop, const Bytes& prog) {
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.tape_auto_play = autop;
            bool ok = emu->init(cfg) && emu->load_tap(tap, fast);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.BC = 0xFFFE; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            return std::pair<std::unique_ptr<Emulator>, bool>(std::move(emu), ok);
        };
        // Steps instruction by instruction to `until` T-states, recording the
        // T-state of every change of the tape EAR level.
        auto step_to = [](Emulator& e, uint64_t until, std::vector<uint64_t>& edges) {
            bool last = e.beeper().tape_ear();
            while (e.monotonic_tstates() < until) {
                e.execute_single_instruction();
                const bool now = e.beeper().tape_ear();
                if (now != last) { edges.push_back(e.monotonic_tstates()); last = now; }
            }
        };

        {   // TAPA-06
            auto [emu, ok] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tape().is_held() && !emu->tape().is_playing() &&
                              emu->tape().current_block() == 1;
            const uint64_t t_hold = emu->monotonic_tstates();
            std::vector<uint64_t> edges;
            uint64_t t_res = 0;
            bool resumed = false;
            while (emu->monotonic_tstates() < t_hold + 40000000ULL) {
                const bool was = emu->beeper().tape_ear();
                emu->execute_single_instruction();
                const bool now = emu->beeper().tape_ear();
                if (now != was) edges.push_back(emu->monotonic_tstates());
                if (!resumed && emu->tape().is_playing()) { resumed = true; t_res = emu->monotonic_tstates(); }
                if (resumed && emu->monotonic_tstates() > t_res + 7500000ULL) break;
            }
            size_t before = 0;
            while (before < edges.size() && edges[before] <= t_res) ++before;
            const size_t silent_edges = before;    // edges while held (up to the resume)
            // The first edges after the resume: a complete data pilot.
            size_t pilot = 0;
            for (size_t i = silent_edges; i + 1 < edges.size(); ++i) {
                const int64_t d = int64_t(edges[i + 1]) - int64_t(edges[i]);
                if (d >= 2168 - 40 && d <= 2168 + 40) ++pilot; else break;
            }
            const int64_t first = silent_edges < edges.size() ? int64_t(edges[silent_edges]) - int64_t(t_res) : -1;
            const size_t k = silent_edges + pilot;     // index of the last pilot edge
            const int64_t sync = (k + 1 < edges.size()) ? int64_t(edges[k + 1]) - int64_t(edges[k]) : -1;
            char b[160];
            std::snprintf(b, sizeof(b), "held=%d f=%d silent=%.1fM edges_while_held=%zu pilot_intervals=%zu sync=%lld",
                          held, f, (t_res - t_hold) / 1e6, silent_edges, pilot, (long long)sync);
            check("TAPA-06", "real time: the deck holds in the gap while the program does not poll, then resumes with a complete data pilot",
                  held && resumed && t_res - t_hold >= 10000000ULL && silent_edges <= 1 &&
                  pilot == 3222 && sync >= 667 - 40 && sync <= 667 + 40 && first > 0 && first <= 2168 + 60, b);
        }
        {   // TAPA-07
            std::vector<uint64_t> on_edges, off_edges;
            bool held_on = false;
            for (int pass = 0; pass < 2; ++pass) {
                auto [emu, ok] = mk(two, false, pass == 0, poll_only);
                (void)ok;
                step_to(*emu, 25000000ULL, pass == 0 ? on_edges : off_edges);
                if (pass == 0) held_on = emu->tape().is_held();
            }
            check("TAPA-07", "a program that polls across every gap sees the same edge timeline with auto play on and off",
                  !held_on && on_edges.size() > 6000 && on_edges == off_edges,
                  "on=" + std::to_string(on_edges.size()) + " off=" + std::to_string(off_edges.size()));
        }
        {   // TAPA-08
            auto [emu, ok] = mk(hdr_two, false, false, delay_then_poll(20));
            std::vector<uint64_t> edges;
            step_to(*emu, 26000000ULL, edges);
            int64_t gap = 0;
            for (size_t i = 0; i + 1 < edges.size(); ++i)
                gap = std::max(gap, int64_t(edges[i + 1]) - int64_t(edges[i]));
            check("TAPA-08", "auto play off: no hold, the next pilot starts exactly 1 s (3,500,000 T) after the last edge",
                  ok && !emu->tape().is_held() && gap >= 3500000 - 50 && gap <= 3500000 + 50,
                  "max gap=" + std::to_string(gap));
        }
        // A "press any key" wait: a tight loop of IN A,(C) for ~12 M T-states (longer
        // than a data block's pilot), then a ROM load entered at 0x0562.
        const Bytes key_wait = {0xF3, 0x01, 0xFE, 0xFF, 0x11, 0x05, 0x00, 0x21, 0x00, 0x00,
                                0xED, 0x78, 0x2B, 0x7C, 0xB5, 0x20, 0xF9, 0x1B, 0x7A, 0xB3, 0x20, 0xF1};
        {   // TAPA-09
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int f = 0; f < 300; ++f) emu->run_frame();
            check("TAPA-09", "fast mode with the defaults: an edge loop never starts the tape (the ROM trap serves the loads)",
                  ok && !emu->tape().is_playing() && !emu->tape().is_held() &&
                  emu->tape().current_block() == 0);
        }
        {   // TAPA-10
            Case c; c.path = two;
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            bool ok = emu->init(cfg) && emu->load_tap(two, true);
            Bytes prog = key_wait;
            Bytes call = caller(c);
            prog.insert(prog.end(), call.begin(), call.end());
            prog.push_back(0x18); prog.push_back(0xFE);
            const uint16_t jr = uint16_t(0x8000 + prog.size() - 2);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            for (int f = 0; f < 400 && emu->cpu().pc() != jr; ++f) emu->run_frame();
            const bool got = emu->cpu().pc() == jr && (emu->cpu().get_registers().AF & 1) &&
                             emu->mmu().peek(0xA000) == 1 && emu->mmu().peek(0xA004) == 5;
            check("TAPA-10", "fast mode: a key-wait longer than the next block's pilot, then a ROM load, still loads that block (GH #395 review F1)",
                  ok && got && !emu->tape().is_playing() && emu->tape().current_block() == 1,
                  "pc=" + std::to_string(emu->cpu().pc()) + " blk=" + std::to_string(emu->tape().current_block()) +
                  " a000=" + std::to_string(emu->mmu().peek(0xA000)));
        }
        {   // TAPA-11
            auto [emu, ok] = mk(two, true, false, poll_only);
            for (int f = 0; f < 100; ++f) emu->run_frame();
            check("TAPA-11", "fast mode, auto play off: an edge loop never starts the tape",
                  ok && !emu->tape().is_playing() && emu->tape().current_block() == 0);
        }
        {   // TAPA-12
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int i = 0; i < 100; ++i) (void)emu->port().read(0x00FE);
            const bool dbg = emu->tape_poll().polling(emu->monotonic_tstates());
            for (int i = 0; i < 100; ++i) emu->execute_single_instruction();
            const bool cpu = emu->tape_poll().polling(emu->monotonic_tstates());
            check("TAPA-12", "reads made by the debugger do not count as polling, the CPU's IN does",
                  ok && !dbg && cpu);
        }
        {   // TAPA-13
            auto [emu, ok] = mk(two, true, true, poll_only);
            while (emu->tape().next_block()) {}
            for (int f = 0; f < 50; ++f) emu->run_frame();
            check("TAPA-13", "a tape at its end is never started by a polling program",
                  ok && emu->tape().at_end() && !emu->tape().is_playing());
        }
        {   // TAPA-14
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int f = 0; f < 3; ++f) emu->run_frame();
            StateWriter m1; emu->save_state(m1);
            std::vector<uint8_t> blob(m1.position());
            StateWriter w1(blob.data(), blob.size()); emu->save_state(w1);
            emu->run_frame();
            const bool was = emu->tape_poll().polling(emu->monotonic_tstates());
            StateReader r1(blob.data(), blob.size());
            const bool loaded = emu->load_state(r1);
            const bool after = emu->tape_poll().polling(emu->monotonic_tstates());
            // A held real-time deck stays held across a state load.
            auto [e2, ok2] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !e2->tape().is_held(); ++f) e2->run_frame();
            StateWriter m2; e2->save_state(m2);
            std::vector<uint8_t> blob2(m2.position());
            StateWriter w2(blob2.data(), blob2.size()); e2->save_state(w2);
            StateReader r2(blob2.data(), blob2.size());
            const bool loaded2 = e2->load_state(r2);
            check("TAPA-14", "a state load resets the detector and leaves a held deck held",
                  ok && ok2 && was && loaded && !after && loaded2 && e2->tape().is_held() &&
                  !e2->tape_poll().polling(e2->monotonic_tstates()),
                  "was=" + std::to_string(was) + " after=" + std::to_string(after) +
                  " held=" + std::to_string(e2->tape().is_held()));
        }
        {   // TAPA-16
            // The resume re-bases the instruction to the read's sample T-state (GH #265), so
            // the first resumed pulse ends 2168 T after that sample, not after the instruction
            // start. A program counts IN A,(C) loops until EAR rises; NOP padding moves the
            // loop phase, and the count changes where the sample point matters. The values
            // are PINNED (measured with the sample point; re-basing to 0 gives 69, 61 and 58
            // for the pads 4, 8 and 10), not derived from an outside oracle.
            static const int want[12] = {83, 79, 75, 71, 70, 67, 65, 63, 62, 60, 59, 57};
            std::string got_s; bool all = true;
            for (int pad = 0; pad < 12; ++pad) {
                Bytes prog = delay_then_poll(20);
                prog.resize(prog.size() - 4);                              // drop the plain poll loop
                prog.insert(prog.end(), {0x21, 0x00, 0x00});               // LD HL,0
                const size_t loop = prog.size();
                prog.push_back(0xED); prog.push_back(0x78);                // IN A,(C)
                prog.push_back(0x23);                                      // INC HL
                for (int i = 0; i < pad; ++i) prog.push_back(0x00);        // NOP padding
                prog.push_back(0xE6); prog.push_back(0x40);                // AND 0x40
                prog.push_back(0x28); prog.push_back(uint8_t(int(loop) - int(prog.size() + 2)));   // JR Z,loop
                prog.push_back(0x18); prog.push_back(0xFE);                // JR $
                auto [emu, ok] = mk(hdr_two, false, true, prog);
                // The program ends in `JR $` with HL final: stop at the first frame boundary that finds it
                // there (frame 487 for every pad), not after the full 900-frame bound it keeps.
                const uint16_t parked = uint16_t(0x8000 + prog.size() - 2);
                for (int f = 0; f < 900 && emu->cpu().pc() != parked; ++f) emu->run_frame();
                const int k = emu->cpu().get_registers().HL;
                got_s += std::to_string(k) + " ";
                all = all && ok && k == want[pad];
            }
            check("TAPA-16", "the resumed deck's first pulse is timed from the read's sample point (pinned loop counts for 12 phases)",
                  all, "got " + got_s);
        }
        {   // TAPA-15
            auto [emu, ok] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tape().is_held() && !emu->tape().is_playing();
            emu->set_tape_auto_play(false);
            check("TAPA-15", "turning auto play off while the deck is held plays it again at once",
                  held && emu->tape().is_playing() && !emu->tape().is_held() && !emu->tape_auto_play());
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
