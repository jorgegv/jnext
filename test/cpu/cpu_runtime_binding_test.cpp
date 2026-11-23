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
//   CRB-12..14  save_state, load_state and rebase_interrupt_window of an unbound CPU act on that CPU
//   CRB-15,16  set_ula_counter_origins and set_tstate_divisor of an unbound CPU land on that CPU
//   CRB-17,18  the T-state divisor and the line memo are each CPU's own across a swap
//   CRB-11  a setter called on a CPU that is not the bound one lands on that CPU
//   CRB-10  the EI-grace stamp and iff2_read are each CPU's own
//   CRB-09  the CPU-level batch accessors (repeat_halt, repeat_loop,
//           capture_loop_state) act on their own CPU, not on the bound one

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "cpu/z80_cpu.h"
#include "memory/contention.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "port/port_dispatch.h"
#include "../row_id.h"

#include <algorithm>
#include <cstdint>
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
        std::fflush(stdout);
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
         0xED, 0x78, 0xFE, 0xB4, 0x28, 0xFA, 0x18, 0xEE});
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
    auto z = [](size_t v) { return v == static_cast<size_t>(-1) ? std::string("none") : std::to_string(v); };
    return "audio " + std::to_string(got.l.size()) + "/" + std::to_string(want.l.size()) +
           " samples first diff " + z(ad) + "; state " + std::to_string(got.st.size()) + "/" +
           std::to_string(want.st.size()) + " bytes first diff " + z(sd);
}

// Bare-CPU rig for CRB-12..18: a 48K ContentionModel and Mmu (slot 2 = page 0x0A, contended),
// and `LD A,(0x4000)` at 0x8000, run from a chosen T-state: the cycle count is a fingerprint of
// the contention phase, hence of the geometry, origins, divisor and line memo in the core.
struct RamM : MemoryInterface {
    uint8_t m[65536] = {};
    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
};
struct NullIo2 : IoInterface {
    uint8_t in(uint16_t) override { return 0xFF; }
    void out(uint16_t, uint8_t) override {}
};

std::vector<uint8_t> save_bytes(Z80Cpu& c) {
    uint8_t buf[512];
    StateWriter w(buf, sizeof(buf));
    c.save_state(w);
    return std::vector<uint8_t>(buf, buf + w.position());
}

struct Rig {
    RamM mem;
    NullIo2 io;
    Ram ram;
    Rom rom;
    ContentionModel cm;
    std::unique_ptr<Mmu> mmu;
    std::unique_ptr<Z80Cpu> cpu;
    Rig() {
        cm.build(MachineType::ZX48K);
        cm.set_cpu_speed(0);
        cm.set_contention_disable(false);
        mmu = std::make_unique<Mmu>(ram, rom);
        mmu->reset(true);
        mmu->set_page(2, 0x0A);
        mem.m[0x8000] = 0x3A; mem.m[0x8001] = 0x00; mem.m[0x8002] = 0x40;
        cpu = std::make_unique<Z80Cpu>(mem, io);
        cpu->set_contention_runtime(&cm, mmu.get(), MachineType::ZX48K);
    }
    uint32_t one(uint32_t t) {
        Z80Registers r = cpu->get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.halted = false;
        cpu->set_registers(r);
        cpu->tstates_ref() = t;
        cpu->execute();
        return cpu->tstates_ref() - t;
    }
    std::vector<uint32_t> sweep(uint32_t from, uint32_t n) {
        std::vector<uint32_t> v;
        for (uint32_t t = from; t < from + n; ++t) v.push_back(one(t));
        return v;
    }
};

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
        uintptr_t after_frame = 0;
        if (no_dangle) {   // never run a machine on a dangling runtime: the row fails by its pointer
            host->run_frame(); drain(*host, oh);
            after_frame = reinterpret_cast<uintptr_t>(z80_bound_contention_for_test());
            frames(*host, oh, kFrames - 1);
            snapshot(*host, oh);
        }
        check("CRB-06",
              "a machine destroyed while it was the bound one leaves no pointer to its ContentionModel in "
              "the core (null or the host's), the host's own after one frame, and the host equals solo",
              no_dangle && (after_destroy == 0 || after_destroy == mine) && after_frame == mine &&
                  oh == poll_solo,
              std::string("dangling=") + (no_dangle ? "no" : "YES") + " after_frame_is_host=" +
                  (after_frame == mine ? "yes" : "NO") + " | " + diff(oh, poll_solo));
    }

    {   // CRB-07
        // A's oracle: the same init history run alone (48K, then re-init as a Next).
        const Out sre = solo([](Emulator& e) { build_48k(e); build_poll(e); });
        auto a = std::make_unique<Emulator>();
        auto b = std::make_unique<Emulator>();
        Out oa, ob;
        build_48k(*a);          // A starts as a 48K machine...
        build_poll(*b);         // ...B is built (and bound) after it...
        build_poll(*a);         // ...and A is re-init()ed as a Next while B was the bound one.
        frames(*b, ob, kFrames); snapshot(*b, ob);   // B first: it must not run on A's re-init
        frames(*a, oa, kFrames); snapshot(*a, oa);
        check("CRB-07",
              "a machine re-init()ed as another machine type while a second one was the bound one, then both "
              "run: each equals the same machine run alone",
              oa == sre && ob == poll_solo,
              "A: " + diff(oa, sre) + " | B: " + diff(ob, poll_solo));
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

    {   // CRB-10: the EI-grace stamp and the iff2_read flag are each CPU's own
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
        ma->m[0] = 0xFB;                                   // A: EI, then NOPs
        mb->m[0] = 0x00; mb->m[1] = 0xFB; mb->m[2] = 0x00; mb->m[3] = 0x00;   // B: NOP EI NOP NOP (own stamp, own clock)
        ma->m[0x38] = 0xED; ma->m[0x39] = 0x4D;            // IM 1 handler of A: RETI
        Z80Registers ra{}; ra.PC = 0; ra.SP = 0xFF00; ra.IM = 1; a.set_registers(ra);
        Z80Registers rb{}; rb.PC = 0; rb.SP = 0xFE00; rb.IM = 1; b.set_registers(rb);
        a.execute();                                       // A: EI; the grace is pending at the next boundary
        b.execute(); b.execute(); b.execute(); b.execute();   // B runs, stamping its own EI at a different T-state
        a.request_interrupt(0xFF, 0, INT64_MAX);           // /INT is already low when A reaches the boundary
        a.execute();                                       // the grace holds: the NOP runs, the /INT is not taken
        const uint16_t pc = a.pc();
        check("CRB-10",
              "a CPU that executed EI keeps its EI grace across another CPU's execute() (and its own stamp, "
              "not the other's): the boundary right after the EI does not take a pending /INT",
              pc == 0x0002, "A.pc=" + std::to_string(pc) + " (0x0002 = grace held, 0x0038 = /INT taken)");
    }

    {   // CRB-11: a setter called on a CPU that is not the bound one lands on that CPU
        struct Ram : MemoryInterface {
            uint8_t read(uint16_t) override { return 0; }
            void write(uint16_t, uint8_t) override {}
        };
        struct NullIo : IoInterface {
            uint8_t in(uint16_t) override { return 0xFF; }
            void out(uint16_t, uint8_t) override {}
        };
        Ram ma, mb;
        NullIo ioa, iob;
        ContentionModel cma, cmb;
        Z80Cpu a(ma, ioa);
        Z80Cpu b(mb, iob);                                  // B is the bound CPU
        b.set_contention_runtime(&cmb, nullptr, MachineType::ZX48K);
        b.set_frame_geometry(240, 240 * 300);
        a.set_contention_runtime(&cma, nullptr, MachineType::ZXN_ISSUE2);   // A is not bound
        a.set_frame_geometry(300, 300 * 100);
        b.tstates_ref();                                    // B again
        const int b_line = z80_get_tstates_per_line();
        const bool b_cm = z80_bound_contention_for_test() == &cmb;
        a.tstates_ref();                                    // A
        const int a_line = z80_get_tstates_per_line();
        const bool a_cm = z80_bound_contention_for_test() == &cma;
        check("CRB-11",
              "set_contention_runtime() and set_frame_geometry() of a CPU that is not the bound one set that "
              "CPU's runtime and line length, and leave the bound CPU's own",
              b_line == 240 && b_cm && a_line == 300 && a_cm,
              "B line=" + std::to_string(b_line) + " B cm own=" + (b_cm ? "yes" : "NO") +
                  " | A line=" + std::to_string(a_line) + " A cm own=" + (a_cm ? "yes" : "NO"));
    }

    const uint32_t T0 = 14336 + 100 * 224;   // display line ~100 of the 48K raster

    {   // CRB-12
        RamM ma, mb; NullIo2 ia, ib;
        ma.m[0] = 0xFB;
        Z80Cpu a(ma, ia);
        Z80Cpu b(mb, ib);
        Z80Registers ra{}; ra.SP = 0xFF00; ra.IM = 1; a.set_registers(ra);
        Z80Registers rb{}; rb.SP = 0xFE00; b.set_registers(rb);
        a.execute();
        const auto s1 = save_bytes(a);          // A bound
        b.execute(); b.execute(); b.execute();
        const auto s2 = save_bytes(a);          // B bound
        check("CRB-12", "save_state() of a CPU that is not the bound one equals its save while bound",
              s1 == s2);
    }

    {   // CRB-13
        RamM ma, mb, md; NullIo2 ia, ib, id;
        ma.m[0] = 0xFB; ma.m[0x38] = 0xED; ma.m[0x39] = 0x4D;
        Z80Cpu d(md, id);
        Z80Registers rd{}; rd.PC = 1; rd.SP = 0xFF00; rd.IM = 1; rd.IFF1 = 1; rd.IFF2 = 1;
        d.set_registers(rd);
        const auto sd = save_bytes(d);          // PC=1, IFF1=1, no EI grace
        Z80Cpu a(ma, ia);
        Z80Cpu b(mb, ib);
        Z80Registers ra{}; ra.SP = 0xFF00; ra.IM = 1; a.set_registers(ra);
        a.execute();                            // A: EI, grace pending
        b.execute();                            // B is bound
        StateReader rr(sd.data(), sd.size());
        a.load_state(rr);                       // A's grace must be cleared
        a.request_interrupt(0xFF, 0, INT64_MAX);
        a.execute();
        check("CRB-13", "load_state() into a CPU that is not the bound one clears that CPU's EI grace "
              "(/INT taken at 0x0038)", a.pc() == 0x0038, "A.pc=" + std::to_string(a.pc()));
    }

    {   // CRB-14
        RamM ma, mb; NullIo2 ia, ib;
        ma.m[0] = 0xFB; ma.m[0x38] = 0xED; ma.m[0x39] = 0x4D;
        Z80Cpu a(ma, ia);
        Z80Cpu b(mb, ib);
        Z80Registers ra{}; ra.SP = 0xFF00; ra.IM = 1; a.set_registers(ra);
        a.execute();                            // EI at T=0, stamp = 4
        b.execute();                            // B is bound
        a.rebase_interrupt_window(4);           // A's stamp must move with its counter
        a.tstates_ref() -= 4;
        a.request_interrupt(0xFF, 0, INT64_MAX);
        a.execute();
        check("CRB-14", "rebase_interrupt_window() of a CPU that is not the bound one moves that CPU's own "
              "EI stamp (grace held, PC 0x0002)", a.pc() == 0x0002, "A.pc=" + std::to_string(a.pc()));
    }

    {   // CRB-15
        std::vector<uint32_t> solo;
        { Rig a; a.cpu->set_ula_counter_origins(120, 70); solo = a.sweep(T0, 64); }
        Rig a; Rig b;                           // B is bound
        a.cpu->set_ula_counter_origins(120, 70);
        const auto got = a.sweep(T0, 64);
        std::vector<uint32_t> dflt;
        { Rig c; dflt = c.sweep(T0, 64); }
        check("CRB-15", "set_ula_counter_origins() on a CPU that is not the bound one lands on that CPU "
              "(the fixture differs from the default origins)", got == solo && solo != dflt);
    }

    {   // CRB-16
        std::vector<uint32_t> solo;
        { Rig a; a.cpu->set_tstate_divisor(4); solo = a.sweep(T0, 64); }
        Rig a; Rig b;
        a.cpu->set_tstate_divisor(4);
        const auto got = a.sweep(T0, 64);
        std::vector<uint32_t> dflt;
        { Rig c; dflt = c.sweep(T0, 64); }
        check("CRB-16", "set_tstate_divisor() on a CPU that is not the bound one lands on that CPU "
              "(the fixture differs from the default divisor)", got == solo && solo != dflt);
    }

    {   // CRB-17
        std::vector<uint32_t> solo;
        { Rig a; a.cpu->set_tstate_divisor(4); solo = a.sweep(T0, 64); }
        Rig a; Rig b;
        a.cpu->set_tstate_divisor(4);
        b.cpu->set_tstate_divisor(8);
        std::vector<uint32_t> got;
        for (uint32_t t = T0; t < T0 + 64; ++t) { got.push_back(a.one(t)); b.one(t); }
        check("CRB-17", "each CPU keeps its own T-state divisor across a swap (A at 4, B at 8, alternating): "
              "A equals A alone", got == solo);
    }

    {   // CRB-18
        auto grid228 = [](Rig& g) { g.cpu->set_frame_geometry(228, 228 * 311); };
        std::vector<uint32_t> solo;
        const uint32_t TB = 101 * 224;          // A's line memo lands on its line 101
        { Rig b; grid228(b); for (uint32_t k = 0; k < 400; ++k) solo.push_back(b.one(TB + k)); }
        Rig a; Rig b; grid228(b);
        std::vector<uint32_t> got;
        for (uint32_t k = 0; k < 400; ++k) { a.one(TB + k); got.push_back(b.one(TB + k)); }
        check("CRB-18", "each CPU keeps its own raster line memo across a swap (A on the 224 grid, B on the "
              "228 grid, alternating): B equals B alone", got == solo);
    }

    std::printf("\nZ80Cpu runtime binding test results\n");
    std::printf("===================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
