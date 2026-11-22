// Z80Cpu runtime binding: two machines in one process do not share CPU state.
//
// The FUSE core keeps its register file, T-state counter, contention runtime and
// raster geometry in process globals. Each Z80Cpu owns a saved copy and binds it
// into the globals when it runs, so a machine built or run while another is alive
// behaves exactly as it does alone.
//
// Oracle: the SAME fixture run ALONE, a machine built, run and destroyed with no
// other machine alive. Expected values are that run's audio samples, its whole
// save_state() stream, never numbers computed by the code under test. The
// hardware behind the fixtures: a machine's 28 MHz read wait follows its OWN
// committed NR 0x07 value (VHDL zxnext.vhd:5816-5820, 5796-5828), and its own
// raster geometry (zxula_timing.vhd).
//
//   CRB-01  two Next machines built, then run one after the other: each equals solo
//   CRB-02  the same, frames alternating
//   CRB-03  EI;JR loop with the frame /INT live (the EI grace and iff2 state), two
//           different programs alternating: each equals solo
//   CRB-04  a second machine built and init()ed leaves the first one's saved state
//           byte-identical
//   CRB-05  a warm-start recorder built after the host: the host equals solo
//   CRB-06  a machine destroyed while it was the bound one leaves no dangling
//           contention runtime, and the host equals solo
//   CRB-07  re-init of a machine while another is the bound one (a different
//           machine type): each equals solo
//   CRB-08  a 48K machine and a Next with different raster geometry: each equals solo
//   CRB-09  the CPU-level batch accessors (repeat_halt, repeat_loop,
//           capture_loop_state) act on their own CPU, not on the bound one

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "cpu/z80_cpu.h"
#include "port/port_dispatch.h"
#include "../row_id.h"

#include <algorithm>
#include <cstdint>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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

void nr_write(Emulator& e, uint8_t reg, uint8_t val) { e.port().out(0x243B, reg); e.port().out(0x253B, val); }
void ay_write(Emulator& e, uint8_t reg, uint8_t val) { e.port().out(0xFFFD, reg); e.port().out(0xBFFD, val); }
void ay_select(Emulator& e, int chip) {
    static const uint8_t cmd[3] = {0xFF, 0xFE, 0xFD};
    e.port().out(0xFFFD, cmd[chip]);
}

void load_and_start(Emulator& e, uint16_t org, const std::vector<uint8_t>& code) {
    for (size_t i = 0; i < code.size(); ++i)
        e.mmu().write(static_cast<uint16_t>(org + i), code[i]);
    Z80Registers r = e.cpu().get_registers();
    r.PC = org; r.SP = 0xBFF0; r.IFF1 = 0; r.IFF2 = 0;
    r.halted = false;
    e.cpu().set_registers(r);
}

// Audible fixture on the Next at 28 MHz: AY0/AY1 tones, and a program polling NR 0x1F
// (the line counter) so the instruction stream depends on the clock, hence on every
// 28 MHz read wait. Same program as the audio fixture of the SCP rows.
void build_poll(Emulator& e) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    e.init(cfg);
    nr_write(e, 0x03, 0x01);
    nr_write(e, 0x08, 0x0A);
    ay_select(e, 0);
    ay_write(e, 7, 0x3E); ay_write(e, 8, 0x0F); ay_write(e, 0, 0x40);
    ay_select(e, 1);
    ay_write(e, 7, 0x3E); ay_write(e, 8, 0x0C); ay_write(e, 0, 0x55);
    nr_write(e, 0x07, 3);
    load_and_start(e, 0xC000,
        {0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79, 0x04,
         0xED, 0x78, 0xFE, 0xB4, 0x20, 0xFA,
         0x21, 0x10, 0xC2, 0x34,
         0xED, 0x78, 0xFE, 0xB4, 0x28, 0xFA, 0x18, 0xEE}, false);
    e.cpu().set_registers([&] { Z80Registers r = e.cpu().get_registers(); r.IM = 0; return r; }());
    e.set_cycle_batching_for_test(true, true, true);
}

// 3.5 MHz Next, IM 2 (software vector table: every vector byte -> 0xC4C4), program A:
// `L: EI; JR L`, so the instruction after every EI is an EI-grace boundary and the frame
// /INT lands on one of them or not. The ISR is `EI; INC (HL); RETI` with HL = 0xC500.
// Program B differs (a `L: EI; NOP; NOP; JR L` loop of another period), so the EI stamp
// of one machine is never the other's.
void build_ei(Emulator& e, bool variant_b) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    e.init(cfg);
    nr_write(e, 0x03, 0x01);
    for (uint16_t a = 0xC200; a <= 0xC300; ++a) e.mmu().write(a, 0xC4);
    const std::vector<uint8_t> isr = {0xFB, 0x21, 0x00, 0xC5, 0x34, 0xED, 0x4D};
    for (size_t i = 0; i < isr.size(); ++i) e.mmu().write(static_cast<uint16_t>(0xC4C4 + i), isr[i]);
    const std::vector<uint8_t> loop_a = {0xFB, 0x18, 0xFD};
    const std::vector<uint8_t> loop_b = {0xFB, 0x00, 0x00, 0x18, 0xFB};
    load_and_start(e, 0xC000, variant_b ? loop_b : loop_a);
    Z80Registers r = e.cpu().get_registers();
    r.IFF1 = 1; r.IFF2 = 1; r.IM = 2; r.I = 0xC2;
    e.cpu().set_registers(r);
}

// 48K machine: a contended-RAM loop that reads, writes and toggles the beeper.
void build_48k(Emulator& e) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 0;
    e.init(cfg);
    const std::vector<uint8_t> code = {0x21, 0x00, 0x40,          // LD HL,0x4000
                                       0x7E,                      // L: LD A,(HL)
                                       0x23,                      // INC HL
                                       0xD3, 0xFE,                // OUT (0xFE),A
                                       0x34,                      // INC (HL)
                                       0x18, 0xF9};               // JR L
    for (size_t i = 0; i < code.size(); ++i) e.mmu().write(static_cast<uint16_t>(0x6000 + i), code[i]);
    Z80Registers r = e.cpu().get_registers();
    r.PC = 0x6000; r.SP = 0x7FF0; r.IFF1 = 0; r.IFF2 = 0;
    r.halted = false;
    e.cpu().set_registers(r);
}

struct Out {
    std::vector<int16_t> l, r;
    std::vector<uint8_t> st;
    bool operator==(const Out& o) const { return l == o.l && r == o.r && st == o.st; }
};

void drain(Emulator& e, Out& o) {
    int16_t buf[2 * 512];
    for (;;) {
        const int n = e.mixer().read_samples(buf, 512);
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) { o.l.push_back(buf[2 * i]); o.r.push_back(buf[2 * i + 1]); }
    }
}

void frames(Emulator& e, Out& o, int n) {
    for (int f = 0; f < n; ++f) { e.run_frame(); drain(e, o); }
}

void snapshot(Emulator& e, Out& o) {
    StateWriter m;
    e.save_state(m);
    o.st.resize(m.position());
    StateWriter w(o.st.data(), o.st.size());
    e.save_state(w);
}

constexpr int kFrames = 8;

template <class Build>
Out solo(Build build, int n = kFrames) {
    auto e = std::make_unique<Emulator>();
    Out o;
    build(*e);
    frames(*e, o, n);
    snapshot(*e, o);
    return o;
}

std::string diff(const Out& got, const Out& want) {
    size_t sd = static_cast<size_t>(-1);
    const size_t n = std::min(got.st.size(), want.st.size());
    for (size_t i = 0; i < n; ++i) if (got.st[i] != want.st[i]) { sd = i; break; }
    size_t ad = static_cast<size_t>(-1);
    const size_t m = std::min(got.l.size(), want.l.size());
    for (size_t i = 0; i < m; ++i) if (got.l[i] != want.l[i] || got.r[i] != want.r[i]) { ad = i; break; }
    char b[160];
    std::snprintf(b, sizeof b, "audio %zu/%zu samples first diff %zd; state %zu/%zu bytes first diff %zd",
                  got.l.size(), want.l.size(), static_cast<ssize_t>(ad), got.st.size(), want.st.size(),
                  static_cast<ssize_t>(sd));
    return b;
}

}  // namespace

int main() {
    const Out poll_solo = solo(build_poll);

    {   // CRB-01
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_poll(*a);
        build_poll(*b);
        frames(*a, oa, kFrames); snapshot(*a, oa);
        frames(*b, ob, kFrames); snapshot(*b, ob);
        check("CRB-01",
              "two Next machines built (28 MHz, NR 0x1F poll, AY tones) and then run one after the other: "
              "each one's audio and saved state equal the same machine run alone",
              oa == poll_solo && ob == poll_solo,
              "A: " + diff(oa, poll_solo) + " | B: " + diff(ob, poll_solo));
    }

    {   // CRB-02
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_poll(*a);
        build_poll(*b);
        for (int f = 0; f < kFrames; ++f) {
            a->run_frame(); drain(*a, oa);
            b->run_frame(); drain(*b, ob);
        }
        snapshot(*a, oa); snapshot(*b, ob);
        check("CRB-02",
              "the same two machines with their frames alternating: each one's audio and saved state "
              "equal the same machine run alone",
              oa == poll_solo && ob == poll_solo,
              "A: " + diff(oa, poll_solo) + " | B: " + diff(ob, poll_solo));
    }

    {   // CRB-03
        const Out sa = solo([](Emulator& e) { build_ei(e, false); });
        const Out sb = solo([](Emulator& e) { build_ei(e, true); });
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_ei(*a, false);
        build_ei(*b, true);
        for (int f = 0; f < kFrames; ++f) {
            a->run_frame(); drain(*a, oa);
            b->run_frame(); drain(*b, ob);
        }
        snapshot(*a, oa); snapshot(*b, ob);
        check("CRB-03",
              "two 3.5 MHz machines running different EI loops with the frame /INT live, frames alternating "
              "(the EI-grace stamp, iff2 and T-state counter are each machine's own): each equals solo",
              oa == sa && ob == sb && sa.st != sb.st,
              "A: " + diff(oa, sa) + " | B: " + diff(ob, sb));
    }

    {   // CRB-04
        auto a = std::make_unique<Emulator>();
        Out oa, before, after;
        build_poll(*a);
        frames(*a, oa, 3);
        snapshot(*a, before);
        auto b = std::make_unique<Emulator>();
        build_poll(*b);
        snapshot(*a, after);
        const Out ref = solo(build_poll, 3);
        check("CRB-04",
              "building and init()ing a second machine leaves the first machine's saved state "
              "byte-identical (and equal to the same machine run alone)",
              before.st == after.st && before.st == ref.st,
              std::string("after vs before: ") + diff(after, before) + " | before vs solo: " + diff(before, ref));
    }

    {   // CRB-05
        auto host = std::make_unique<Emulator>();
        Out oh;
        build_poll(*host);
        auto rec = std::make_unique<Emulator>();
        build_poll(*rec);   // the warm-start recorder: a real second machine, init()ed after the host
        host->set_warm_start_recorder_for_test(std::move(rec));
        frames(*host, oh, kFrames);
        snapshot(*host, oh);
        check("CRB-05",
              "a warm-start recorder built and init()ed after the host does not change the host's "
              "audio or saved state: equal to the host run alone",
              oh == poll_solo, diff(oh, poll_solo));
    }

    {   // CRB-06
        auto host = std::make_unique<Emulator>();
        build_poll(*host);
        auto second = std::make_unique<Emulator>();
        build_poll(*second);
        const auto dead = reinterpret_cast<uintptr_t>(&second->contention());
        second.reset();
        const auto after_destroy = reinterpret_cast<uintptr_t>(z80_bound_contention_for_test());
        const auto mine = reinterpret_cast<uintptr_t>(&host->contention());
        const bool no_dangle = after_destroy != dead;
        Out oh;
        host->run_frame(); drain(*host, oh);
        const auto after_frame = reinterpret_cast<uintptr_t>(z80_bound_contention_for_test());
        frames(*host, oh, kFrames - 1);
        snapshot(*host, oh);
        check("CRB-06",
              "a machine destroyed while it was the bound one leaves no pointer to its ContentionModel in "
              "the core (null or the host's), the host's own after one frame, and the host equals solo",
              no_dangle && (after_destroy == 0 || after_destroy == mine) && after_frame == mine &&
                  oh == poll_solo,
              std::string("dangling=") + (no_dangle ? "no" : "YES") + " after_frame_is_host=" +
                  (after_frame == mine ? "yes" : "NO") + " | " + diff(oh, poll_solo));
    }

    {   // CRB-07
        const Out s48 = solo(build_48k, 4);
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_48k(*a);          // A starts as a 48K machine...
        build_poll(*b);         // ...B is built (and bound) after it...
        build_poll(*a);         // ...and A is re-init()ed as a Next while B was the bound one.
        frames(*a, oa, kFrames); snapshot(*a, oa);
        frames(*b, ob, kFrames); snapshot(*b, ob);
        check("CRB-07",
              "a machine re-init()ed as another machine type while a second one was the bound one, then both "
              "run: each equals the same machine run alone",
              oa == poll_solo && ob == poll_solo && !(s48 == poll_solo),
              "A: " + diff(oa, poll_solo) + " | B: " + diff(ob, poll_solo));
    }

    {   // CRB-08
        const Out s48 = solo(build_48k, 4);
        const Out snx = solo(build_poll, 4);
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_48k(*a);
        build_poll(*b);         // the last-built machine is the Next
        frames(*a, oa, 4); snapshot(*a, oa);
        frames(*b, ob, 4); snapshot(*b, ob);
        check("CRB-08",
              "a 48K machine and a Next, both built, each run 4 frames: the 48K's contention, geometry and "
              "ULA origins are its own, not the last-built machine's; each equals solo",
              oa == s48 && ob == snx,
              "48K: " + diff(oa, s48) + " | Next: " + diff(ob, snx));
    }

    {   // CRB-09: the batch accessors on a CPU that is not the bound one
        struct Ram : MemoryInterface {
            uint8_t m[65536] = {};
            uint8_t read(uint16_t a) override { return m[a]; }
            void write(uint16_t a, uint8_t v) override { m[a] = v; }
        };
        struct NullIo : IoInterface {
            uint8_t in(uint16_t) override { return 0xFF; }
            void out(uint16_t, uint8_t) override {}
        };
        auto ma = std::make_unique<Ram>(); auto mb = std::make_unique<Ram>();
        NullIo ioa, iob;
        Z80Cpu a(*ma, ioa);
        Z80Cpu b(*mb, iob);
        ma->m[0] = 0x76;   // A: HALT
        mb->m[0] = 0x00; mb->m[1] = 0x00; mb->m[2] = 0x18; mb->m[3] = 0xFC;   // B: NOP NOP JR -4
        Z80Registers ra{}; ra.PC = 0; ra.SP = 0xFF00; a.set_registers(ra);
        Z80Registers rb{}; rb.PC = 0; rb.SP = 0xFE00; rb.BC = 0x1234; b.set_registers(rb);
        a.execute();                 // A halts (bound: A)
        const uint32_t ta = a.tstates_ref();
        b.execute(); b.execute();    // B runs NOP NOP (bound: B)
        a.repeat_halt(10, 4);        // A is not the bound CPU
        b.execute();                 // (B is bound again)
        a.repeat_loop(3, 10, 3, 2);  // and again
        const uint32_t a_after = a.tstates_ref();
        const uint8_t  r_a = a.registers().R & 0x7f;
        const auto ls = a.capture_loop_state();
        const uint32_t tb = b.tstates_ref();
        // Nothing spent on A's repeats may land in B's counter, and A's loop state is A's.
        check("CRB-09",
              "repeat_halt(), repeat_loop() and capture_loop_state() of a CPU that is not the bound one act on "
              "that CPU: its counter moves by count*each, B's does not, and the captured state is its own",
              a_after == ta + 40 + 30 && r_a == ((1 + 10 + 6) & 0x7f) && ls.bc == 0 && ls.sp == 0xFF00 && ls.halted &&
                  tb == 4 + 4 + 12,
              "a: " + std::to_string(ta) + " -> " + std::to_string(a_after) + " R=" + std::to_string(r_a) +
                  " bc=" + std::to_string(ls.bc) + " sp=" + std::to_string(ls.sp) + " b: " + std::to_string(tb));
    }

    std::printf("\nZ80Cpu runtime binding test results\n");
    std::printf("===================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
