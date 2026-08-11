// --inject entry-state tests (GitHub issue #248).
//
// No VHDL oracle: `--inject` has no counterpart on a ZX Next. It is a jnext
// host debugging aid that forces a program and an entry point into a running
// machine, so its oracle is the contract stated on Emulator::inject_binary and
// in issue #248:
//
//   the CPU state the injected program starts from must describe THAT
//   program, never the one it displaced.
//
// The displaced program is normally NextZXOS, which idles in a `HALT` waiting
// for its 50 Hz interrupt — so at any plausible --inject-delay the CPU is
// halted when the injection lands. `halted` is a latch the injected program
// cannot clear itself (it has just been DI'd, and FUSE clears the flag only
// when it accepts an interrupt or an NMI), so it survives into the new program
// and makes its FIRST interrupt or NMI return one byte late:
// fuse_z80_core.c:140/:177/:192 do `if (halted) { PC++; halted = 0; }` before
// pushing, and z80_cpu.cpp:636 mirrors that +1 into the NR 0xC2/0xC3 NMI
// return latch. Every later interrupt is exact, so the symptom is a tight loop
// derailed by its first interrupt only.
//
// The rows below are behavioural where it matters: INJ-HALT-02/03/04 read the
// pushed return address back out of the stack after a real interrupt or NMI,
// which is the user-visible defect. INJ-HALT-01 pins the mechanism.
//
// Run: ./build/test/inject_test

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// ── Test infrastructure (feedback_uniform_test_output) ────────────────

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

struct Result {
    std::string group, id, desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {})
{
    ++g_total;
    g_results.push_back({g_group, id, desc, cond, detail});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string hex16(uint16_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04x", v);
    return std::string(b);
}

// ── Fixture ───────────────────────────────────────────────────────────

// A scratch file holding the bytes --inject will load. Removed on destruction
// so a failing row cannot leave litter behind.
class ScratchBinary {
public:
    ScratchBinary(const char* name, const std::vector<uint8_t>& bytes)
    {
        const char* dir = std::getenv("TMPDIR");
        path_ = std::string(dir ? dir : "/tmp") + "/jnext-inject-test-" + name + ".bin";
        FILE* f = std::fopen(path_.c_str(), "wb");
        if (f) {
            if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            ok_ = true;
        }
    }
    ~ScratchBinary() { if (ok_) std::remove(path_.c_str()); }

    const std::string& path() const { return path_; }
    bool ok() const { return ok_; }

private:
    std::string path_;
    bool        ok_ = false;
};

void build_next_emulator(Emulator& emu)
{
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    // Leave config_mode, which force-clears every NMI latch while set
    // (zxnext.vhd:2102-2105), so the NMI rows below can take an NMI at all.
    emu.port().out(0x243B, 0x03);
    emu.port().out(0x253B, 0x01);
}

// Drive the CPU into a REAL halt by executing a `HALT` opcode, rather than
// forcing regs.halted — the point is to reproduce the state NextZXOS's idle
// loop leaves behind, and a forced flag would not prove the CPU agrees.
// Returns true if the CPU is halted afterwards.
bool halt_cpu_at(Emulator& emu, uint16_t addr)
{
    emu.mmu().write(addr, 0x76);            // HALT
    auto regs = emu.cpu().get_registers();
    regs.PC   = addr;
    regs.SP   = 0xFFF0;
    regs.IFF1 = 1;                          // NextZXOS idles with interrupts on
    regs.IFF2 = 1;
    regs.IM   = 1;
    regs.halted = false;
    emu.cpu().set_registers(regs);
    emu.cpu().execute();                    // executes the HALT
    return emu.cpu().is_halted();
}

// Read the 16-bit word the CPU pushed at the current SP.
uint16_t stacked_word(Emulator& emu)
{
    const uint16_t sp = emu.cpu().get_registers().SP;
    return static_cast<uint16_t>(emu.mmu().read(sp) |
                                 (emu.mmu().read(static_cast<uint16_t>(sp + 1)) << 8));
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// Section INJ-HALT — the halted latch must not survive an injection
// ══════════════════════════════════════════════════════════════════════

// The injected payload: `JR $` at the entry point. An exact interrupt return
// loops there for ever; a +1 return lands on the displacement byte and runs
// away, which is precisely the reported symptom.
static const std::vector<uint8_t> kSpinPayload = { 0x18, 0xFE };   // jr $

static void test_inj_halt_rows()
{
    set_group("INJ-HALT");

    const uint16_t kOrg = 0x9000;

    ScratchBinary bin("spin", kSpinPayload);
    if (!bin.ok()) {
        check("INJ-HALT-00", "scratch payload file created", false,
              "cannot write " + bin.path());
        return;
    }

    // INJ-HALT-01 — the mechanism. A CPU halted in the displaced program is
    // no longer halted once --inject has named a new entry point, and the
    // other entry-state registers inject_binary documents are set with it.
    {
        Emulator emu;
        build_next_emulator(emu);
        const bool was_halted = halt_cpu_at(emu, 0x8000);
        check("INJ-HALT-01a", "fixture: CPU really is halted before injecting",
              was_halted);

        const bool loaded = emu.inject_binary(bin.path(), kOrg, kOrg);
        check("INJ-HALT-01b", "inject_binary() succeeds", loaded);

        const auto regs = emu.cpu().get_registers();
        check("INJ-HALT-01c", "halted latch cleared by injection", !regs.halted);
        check("INJ-HALT-01d", "PC is the requested entry point",
              regs.PC == kOrg, hex16(regs.PC));
        check("INJ-HALT-01e", "interrupts disabled for the injected program",
              regs.IFF1 == 0 && regs.IFF2 == 0);
    }

    // INJ-HALT-02 — the defect itself, for a maskable interrupt. IM 1 pushes
    // the interrupted PC and jumps to 0x0038; the pushed word must be the
    // entry point, not entry+1. Pre-fix this row reads 0x9001.
    {
        Emulator emu;
        build_next_emulator(emu);
        halt_cpu_at(emu, 0x8000);
        emu.inject_binary(bin.path(), kOrg, kOrg);

        auto regs = emu.cpu().get_registers();
        regs.IFF1 = 1;                      // the injected program EI'd
        regs.IFF2 = 1;
        regs.IM   = 1;
        emu.cpu().set_registers(regs);

        emu.cpu().execute();                // the `jr $` at the entry point
        check("INJ-HALT-02a", "injected program still at its entry point",
              emu.cpu().get_registers().PC == kOrg,
              hex16(emu.cpu().get_registers().PC));

        emu.cpu().request_interrupt(0xFF);
        emu.cpu().execute();                // accepts the INT

        check("INJ-HALT-02b", "INT accepted (IM 1 vector 0x0038)",
              emu.cpu().get_registers().PC == 0x0038,
              hex16(emu.cpu().get_registers().PC));
        check("INJ-HALT-02c", "INT stacks the interrupted PC, not PC+1",
              stacked_word(emu) == kOrg, hex16(stacked_word(emu)));
    }

    // INJ-HALT-03 — same for an NMI, which takes an independent path through
    // fuse_z80_nmi() and is not gated by IFF1. Issue #248 measured the +1
    // here too, so a fix that only covered the INT path would be partial.
    {
        Emulator emu;
        build_next_emulator(emu);
        halt_cpu_at(emu, 0x8000);
        emu.inject_binary(bin.path(), kOrg, kOrg);

        emu.cpu().execute();                // the `jr $`
        emu.cpu().request_nmi();
        emu.cpu().execute();                // accepts the NMI

        check("INJ-HALT-03a", "NMI accepted (vector 0x0066)",
              emu.cpu().get_registers().PC == 0x0066,
              hex16(emu.cpu().get_registers().PC));
        check("INJ-HALT-03b", "NMI stacks the interrupted PC, not PC+1",
              stacked_word(emu) == kOrg, hex16(stacked_word(emu)));
    }

    // INJ-HALT-04 — the control, one instruction apart: a CPU that was NOT
    // halted when the injection landed already behaved correctly, and must
    // keep doing so. Without this row a "clear halted unconditionally" fix
    // and a "never halt at all" bug are indistinguishable.
    {
        Emulator emu;
        build_next_emulator(emu);
        // No halt_cpu_at(): the displaced program is running, not halted.
        check("INJ-HALT-04a", "fixture: CPU is not halted before injecting",
              !emu.cpu().is_halted());

        emu.inject_binary(bin.path(), kOrg, kOrg);
        emu.cpu().execute();                // the `jr $`
        emu.cpu().request_nmi();
        emu.cpu().execute();

        check("INJ-HALT-04b", "NMI from a never-halted CPU still stacks PC",
              stacked_word(emu) == kOrg, hex16(stacked_word(emu)));
    }

    // INJ-HALT-05 — a genuine HALT *inside* the injected program keeps the
    // Z80's documented +1 return. Clearing the latch at injection time must
    // not disturb the CPU's own halt semantics afterwards; this is the row a
    // fix that clobbers `halted` on every execute() would fail.
    {
        Emulator emu;
        build_next_emulator(emu);
        halt_cpu_at(emu, 0x8000);
        emu.inject_binary(bin.path(), kOrg, kOrg);

        emu.mmu().write(kOrg, 0x76);        // replace `jr $` with HALT
        emu.cpu().execute();                // executes the injected HALT
        check("INJ-HALT-05a", "injected HALT halts the CPU",
              emu.cpu().is_halted());

        emu.cpu().request_nmi();
        emu.cpu().execute();
        check("INJ-HALT-05b", "a real HALT still returns to PC+1",
              stacked_word(emu) == static_cast<uint16_t>(kOrg + 1),
              hex16(stacked_word(emu)));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main()
{
    std::printf("--inject entry-state tests (GH #248)\n");
    std::printf("===============================================\n\n");

    test_inj_halt_rows();
    std::printf("  Group: INJ-HALT — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
