// z80_cpu.cpp — Z80Cpu wrapper backed by FUSE Z80 core.
// 
// Design notes
// ────────────
// z80_cpu.h is left unchanged (no FUSE types may appear there).  The FUSE Z80
// core uses a global `processor z80` struct and global `tstates` counter.
// We provide the memory/IO callback functions that the FUSE opcode files call
// via macros defined in fuse_z80_shim.h.
// 
// The FUSE core is inherently single-instance (global state).  This matches
// our emulator's single-CPU architecture.
// 
// Z80N opcode interception
// ────────────────────────
// FUSE's Z80 core treats undefined ED-prefixed opcodes as NOPs.  Before calling
// fuse_z80_execute_one() we peek at the opcode at PC.  If it is 0xED followed
// by a Z80N-specific byte we dispatch to execute_z80n() and advance PC ourselves.

#include "z80_cpu.h"
#include "z80n_ext.h"
#include "core/log.h"
#include "core/saveable.h"
#include "memory/contention.h"
#include "memory/mmu.h"

#include <cstdint>
#include <cstdlib>

// ── FUSE Z80 core (C linkage) ───────────────────────────────────────────

extern "C" {
#include "fuse_z80_shim.h"
}

// G53 (2026-05-01) — the legacy FUSE contention tables (memory_map_read,
// memory_map_write, ula_contention, ula_contention_no_mreq) and their
// builders/setters are retired. The FUSE opcode TU is now built with
// CORETEST defined (src/cpu/CMakeLists.txt) so contend_read /
// contend_read_no_mreq / contend_write_no_mreq are extern function
// calls implemented below — they go through ContentionModel::contention_tick()
// instead of indexing zero-filled tables. No code in the codebase reads
// the tables; the FUSE shim's extern declarations were removed in lockstep.

// ── Memory/IO callback state ────────────────────────────────────────────

// The FUSE opcode files call readbyte/writebyte/readport/writeport which
// are macros that expand to fuse_z80_readbyte() etc.  These C functions
// dispatch through the active Z80Cpu instance's MemoryInterface/IoInterface.
static MemoryInterface* s_mem = nullptr;
static IoInterface*     s_io  = nullptr;

// Contention callback: called on each memory access to a potentially
// contended address. The callback adds the contention delay to tstates.
static std::function<void(uint16_t addr)>* s_contention_cb = nullptr;

// ── Phase-2 contention runtime (2026-04-26) + G141 (2026-05-01) ────────
// Per-cycle VHDL-faithful contention via ContentionModel::contention_tick().
// Set by Emulator::init() through z80_set_contention_runtime(); when null,
// no contention is applied (FUSE Z80 test harness path — preserves the
// 1356/1356 compliance score).
//
// G141: the FUSE in-opcode contend_read[_no_mreq]/_write_no_mreq macros
// (z80_macros.h:107-130) are also routed here via the CORETEST function-
// override path (-DCORETEST in src/cpu/CMakeLists.txt). All four sites —
// the four C callbacks below and the three CORETEST overrides — share the
// same `s_contention` gate, so on the FUSE-Z80 standalone path they all
// stay inert.
static ContentionModel* s_contention      = nullptr;
static Mmu*             s_contention_mmu  = nullptr;
static int              s_tstates_per_line = 224;
static int              s_tstates_per_frame = 224 * 312;

namespace {

/// Compute (hc, vc) in the 7 MHz pixel-tick domain from the FUSE
/// frame-relative tstates counter.
///
/// VHDL `i_hc` / `i_vc` are 9-bit counters in the 7 MHz pixel-tick
/// domain (each T-state = 2 pixel ticks). Frame is reset to 0 at
/// frame start in Emulator::run_frame().
struct HcVc { uint16_t hc; uint16_t vc; };
inline HcVc derive_hc_vc(uint32_t tstates) {
    int frame_ts = static_cast<int>(tstates % static_cast<uint32_t>(s_tstates_per_frame));
    int line     = frame_ts / s_tstates_per_line;
    int ts_in_line = frame_ts - line * s_tstates_per_line;
    int hc = ts_in_line * 2;          // 1 T-state = 2 pixel ticks
    return {static_cast<uint16_t>(hc), static_cast<uint16_t>(line)};
}

/// VHDL `mem_active_page` — the SRAM page underlying a memory cycle's
/// 16-bit Z80 address. Mmu::get_effective_page() returns the mapped
/// page for an 8K slot; 0xFF means no SRAM (ROM/peripheral).
inline uint8_t mem_active_page_for(uint16_t address) {
    if (!s_contention_mmu) return 0xFF;
    int slot = address >> 13;
    return s_contention_mmu->get_effective_page(slot);
}

} // anonymous namespace

extern "C" {

// Raw memory read — no timing, no contention.  Used for opcode fetches
// where contend_read() already handled the timing.
libspectrum_byte fuse_z80_readbyte_raw(libspectrum_word address) {
    return s_mem->read(address);
}

// Data memory read — adds contention + 3 T-states, matching original FUSE
// readbyte() from memory_pages.c.  Used for instruction data operands.
libspectrum_byte fuse_z80_readbyte(libspectrum_word address) {
    if (s_contention) {
        // VHDL `cpu_mreq_n='0', cpu_iorq_n='1', cpu_rd_n='0'` — memory read.
        s_contention->set_mem_active_page(mem_active_page_for(address));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/false, /*iorq_n*/true,
            /*rd_n*/false,  /*wr_n*/true,
            address, pos.hc, pos.vc);
    }
    tstates += 3;
    return s_mem->read(address);
}

// Data memory write — adds contention + 3 T-states, matching original FUSE
// writebyte() from memory_pages.c.
void fuse_z80_writebyte(libspectrum_word address, libspectrum_byte b) {
    if (s_contention) {
        // VHDL `cpu_mreq_n='0', cpu_iorq_n='1', cpu_wr_n='0'` — memory write.
        s_contention->set_mem_active_page(mem_active_page_for(address));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/false, /*iorq_n*/true,
            /*rd_n*/true,   /*wr_n*/false,
            address, pos.hc, pos.vc);
    }
    tstates += 3;
    s_mem->write(address, b);
}

// Expose tstates for contention callback to add delays
libspectrum_dword* fuse_z80_tstates_ptr(void) { return &tstates; }

libspectrum_byte fuse_z80_readport(libspectrum_word port) {
    // VHDL: port reads consume 1 T-state of pre-IORQ + 3 T-states of
    // post-IORQ (FUSE timing model). Real-hardware contention fires on
    // the IORQ falling edge; we approximate this as a single
    // contention_tick at the start of the post-IORQ phase, when
    // `cpu_iorq_n` would go low.
    tstates++;
    libspectrum_byte val = s_io->in(port);
    if (s_contention) {
        // mem_active_page is irrelevant for port cycles (mem_contend=0);
        // contention_tick gates on port_contend internally.
        s_contention->set_mem_active_page(mem_active_page_for(port));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/true,  /*iorq_n*/false,
            /*rd_n*/false,   /*wr_n*/true,
            port, pos.hc, pos.vc);
    }
    tstates += 3;
    return val;
}

void fuse_z80_writeport(libspectrum_word port, libspectrum_byte b) {
    tstates++;
    s_io->out(port, b);
    if (s_contention) {
        s_contention->set_mem_active_page(mem_active_page_for(port));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/true,  /*iorq_n*/false,
            /*rd_n*/true,    /*wr_n*/false,
            port, pos.hc, pos.vc);
    }
    tstates += 3;
}

// ── G141 (2026-05-01) — FUSE in-opcode contention overrides ────────────
// The FUSE Z80 opcode files use contend_read / contend_read_no_mreq /
// contend_write_no_mreq macros (third_party/fuse-z80/z80_macros.h:109-122)
// for M1 fetch contention and the no-MREQ tail cycles inside multi-cycle
// instructions (LDIR/LDDR/EX(SP)/DJNZ/etc.). Built with -DCORETEST
// (src/cpu/CMakeLists.txt) the macros become extern function calls; we
// implement them here so every in-opcode contention point flows through
// `ContentionModel::contention_tick()` — the same VHDL-faithful gate the
// data path uses. This is the G141 fix: every cycle on a contended page
// during the active raster window now adds the correct stretch instead
// of zero (the legacy ula_contention[]/memory_map_read[] tables and
// their builders/setters were retired in lockstep — G53).
//
// All three functions pass `mreq_n=false` even though the no-MREQ macros
// nominally represent a non-MREQ tail cycle. Rationale (VHDL oracle —
// zxula.vhd:582-600):
//
//   * `wait_s` (line 582-583) gates ON THE WINDOW — `(hc_adj × vc ×
//     contention_en)` — independent of MREQ. So both data and no-MREQ
//     macros need to enter contention_tick() during the same window.
//   * `o_cpu_contend` for 48K/128K (line 595) fires on:
//       (mem_c AND mreq23_n='1') OR (port_c AND iorq_n='0' ...)
//     where `mreq23_n` is the REGISTERED prior-cycle MREQ_n. The path
//     therefore fires on contended-memory cycles whether the CURRENT
//     MREQ is asserted or not — the registered version captures the
//     in-opcode tail.
//   * `o_cpu_wait_n` for +3 (line 600) fires on:
//       (mreq_n='0' AND mem_c) AND timing_p3 AND wait_s
//     i.e. only the asserted-MREQ memory path.
//
// jnext's `contention_tick()` collapses both paths into a single returned
// stretch keyed on `(hc, vc)` and triggers the memory side on
// `mreq_n == false && mem_c`. Passing `mreq_n=false` for all three macro
// overrides therefore matches the 48K/128K registered-MREQ semantics
// (Path A) and the +3 asserted-MREQ semantics (the only +3 path) — both
// machines now produce the same per-cycle stretch the VHDL would emit.
//
// The conceptual model: the FUSE macros represent a cycle on the address
// bus that touches a memory page; the contention gate keys on the page
// being contended AND the raster window being active, NOT on whether the
// CPU happens to assert MREQ on the wire that exact T-state. This is the
// same model the data callbacks above use.
extern "C" void contend_read(libspectrum_word address, libspectrum_dword time) {
    if (s_contention) {
        s_contention->set_mem_active_page(mem_active_page_for(address));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/false, /*iorq_n*/true,
            /*rd_n*/false,   /*wr_n*/true,
            address, pos.hc, pos.vc);
    }
    tstates += time;
}

extern "C" void contend_read_no_mreq(libspectrum_word address, libspectrum_dword time) {
    if (s_contention) {
        s_contention->set_mem_active_page(mem_active_page_for(address));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/false, /*iorq_n*/true,
            /*rd_n*/true,    /*wr_n*/true,
            address, pos.hc, pos.vc);
    }
    tstates += time;
}

extern "C" void contend_write_no_mreq(libspectrum_word address, libspectrum_dword time) {
    if (s_contention) {
        s_contention->set_mem_active_page(mem_active_page_for(address));
        auto pos = derive_hc_vc(tstates);
        tstates += s_contention->contention_tick(
            /*mreq_n*/false, /*iorq_n*/true,
            /*rd_n*/true,    /*wr_n*/true,
            address, pos.hc, pos.vc);
    }
    tstates += time;
}

} // extern "C"

// ── Z80N opcode lookup table ────────────────────────────────────────────

static bool kZ80NOpcodeTable[256] = {};

static bool init_z80n_table() {
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::SWAPNIB)]    = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::MIRROR_A)]   = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::TEST_N)]     = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::BSLA_DE_B)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::BSRA_DE_B)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::BSRL_DE_B)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::BSRF_DE_B)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::BRLC_DE_B)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::MUL_DE)]     = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_HL_A)]   = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_DE_A)]   = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_BC_A)]   = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_HL_NN)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_DE_NN)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::ADD_BC_NN)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::PUSH_NN)]    = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::OUTINB)]     = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::NEXTREG_NN)] = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::NEXTREG_A)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::PIXELDN)]    = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::PIXELAD)]    = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::SETAE)]      = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::JP_C)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIX)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDWS)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDDX)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIRX)]      = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIRSCALE)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDPIRX)]     = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDDRX)]      = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LOOP)]       = true;
    return true;
}
static bool z80n_table_initialized = init_z80n_table();

// ── Register sync helpers ───────────────────────────────────────────────

static void sync_regs_from_fuse(Z80Registers& r) {
    r.AF  = z80.af.w;
    r.BC  = z80.bc.w;
    r.DE  = z80.de.w;
    r.HL  = z80.hl.w;
    r.AF2 = z80.af_.w;
    r.BC2 = z80.bc_.w;
    r.DE2 = z80.de_.w;
    r.HL2 = z80.hl_.w;
    r.IX  = z80.ix.w;
    r.IY  = z80.iy.w;
    r.SP  = z80.sp.w;
    r.PC  = z80.pc.w;
    r.I   = z80.i;
    r.R   = (z80.r7 & 0x80) | (z80.r & 0x7f);
    r.MEMPTR = z80.memptr.w;
    r.IFF1 = z80.iff1;
    r.IFF2 = z80.iff2;
    r.IM   = z80.im;
    r.Q    = z80.q;
    r.halted = (z80.halted != 0);
}

static void sync_fuse_from_regs(const Z80Registers& r) {
    z80.af.w  = r.AF;
    z80.bc.w  = r.BC;
    z80.de.w  = r.DE;
    z80.hl.w  = r.HL;
    z80.af_.w = r.AF2;
    z80.bc_.w = r.BC2;
    z80.de_.w = r.DE2;
    z80.hl_.w = r.HL2;
    z80.ix.w  = r.IX;
    z80.iy.w  = r.IY;
    z80.sp.w  = r.SP;
    z80.pc.w  = r.PC;
    z80.i     = r.I;
    z80.r     = r.R & 0x7f;
    z80.r7    = r.R & 0x80;
    z80.memptr.w = r.MEMPTR;
    z80.iff1  = r.IFF1;
    z80.iff2  = r.IFF2;
    z80.im    = r.IM;
    z80.q     = r.Q;
    z80.halted = r.halted ? 1 : 0;
}

// ══════════════════════════════════════════════════════════════════════════
// Z80Cpu — public implementation
// ══════════════════════════════════════════════════════════════════════════

static bool s_tables_initialized = false;

Z80Cpu::Z80Cpu(MemoryInterface& mem, IoInterface& io)
    : mem_(mem), io_(io)
{
    s_mem = &mem_;
    s_io  = &io_;

    if (!s_tables_initialized) {
        fuse_z80_init_tables();
        s_tables_initialized = true;
    }

    reset();
}

void Z80Cpu::reset() {
    s_mem = &mem_;
    s_io  = &io_;

    fuse_z80_reset(1); // hard reset
    tstates = 0;

    nmi_pending_ = false;
    int_pending_ = false;
    int_vector_  = 0xFF;

    sync_regs_from_fuse(regs_);
}

int Z80Cpu::execute() {
    s_mem = &mem_;
    s_io  = &io_;
    s_contention_cb = &on_contention;

    // Push any externally set registers into FUSE state
    sync_fuse_from_regs(regs_);

    // ── NMI ────────────────────────────────────────────────────────────
    if (nmi_pending_) {
        nmi_pending_ = false;
        Log::cpu()->debug("NMI at PC={:#06x}", z80.pc.w);
        // G88: capture the PC that will be pushed to stack as the NMI return
        // address (NR 0xC2 LSB / NR 0xC3 MSB shadow). Mirrors VHDL
        // zxnext.vhd:2050-2085, 6232-6236 (Z80N_command_s = NMIACK_LSB/MSB
        // latch). fuse_z80_nmi() bumps PC by +1 if HALTed before pushing, so
        // pre-compute the stacked value here.
        if (on_nmi_servicing) {
            uint16_t saved_pc = z80.halted ? static_cast<uint16_t>((z80.pc.w + 1) & 0xFFFF)
                                            : z80.pc.w;
            on_nmi_servicing(saved_pc);
        }
        libspectrum_dword before = tstates;
        fuse_z80_nmi();
        sync_regs_from_fuse(regs_);
        int cycles = (int)(tstates - before);
        return (cycles > 0) ? cycles : 11;
    }

    // ── INT ────────────────────────────────────────────────────────────
    // Real hardware holds /INT low for ~32 T-states (pulse).  If the CPU
    // doesn't acknowledge within that window (e.g. interrupts are disabled
    // inside an ISR), the interrupt is missed.  Without this, a pending
    // interrupt persists indefinitely and fires the moment EI/RETI re-enables
    // interrupts — even frames later — breaking programs that call
    // waitForScanline() inside their ISR.
    static constexpr uint32_t INT_PULSE_TSTATES = 32;
    if (int_pending_) {
        if (tstates - int_requested_at_ > INT_PULSE_TSTATES && !z80.iff1) {
            // Pulse expired while interrupts were disabled — missed.
            int_pending_ = false;
        } else if (z80.iff1) {
            // Resolve the vector byte for this IntAck cycle.
            //
            // Opt-in IM2 daisy-chain path: when on_int_ack is installed
            // (Im2Controller wiring in the emulator), call it at the
            // IntAck M1 cycle and use its returned byte as the vector.
            // This mirrors VHDL im2_device.vhd:155 driving o_vec during
            // S_ACK, OR-reduced by peripherals.vhd:134-144, captured at
            // the CPU IntAck cycle per zxnext.vhd:1999.
            //
            // When not installed, fall back to the legacy int_vector_
            // member (set via request_interrupt()). FUSE Z80 tests never
            // install this callback, preserving 1356/1356 compliance.
            uint8_t vector = on_int_ack ? on_int_ack() : int_vector_;
            Log::cpu()->debug("INT vector={:#04x} at PC={:#06x}", vector, z80.pc.w);
            libspectrum_dword before = tstates;
            int accepted = fuse_z80_interrupt(vector);
            sync_regs_from_fuse(regs_);
            if (accepted) {
                int_pending_ = false;
                return (int)(tstates - before);
            }
            // If not accepted (e.g. interrupts_enabled_at == tstates), keep
            // int_pending_ true so the interrupt is retried next cycle.
            // Fall through to execute one instruction first.
        }
    }

    // ── Z80N interception ──────────────────────────────────────────────
    uint16_t pc = z80.pc.w;

    // DivMMC automap (and any other memory overlay) must activate BEFORE
    // the opcode read, matching real hardware combinatorial decode.
    if (on_m1_prefetch) on_m1_prefetch(pc);

    uint8_t  opcode = mem_.read(pc);
    // G46(b) DIAGNOSTIC ($5b8a / $5b8e watchpoint): track changes to the
    // wrapper-saved sysvars across CPU steps. The previous instruction caused
    // the change, so attribute it to prev_pc. Gate behind JNEXT_G46B_WATCH so
    // the per-step memory reads only happen when actively investigating.
    {
        static const bool g46b_watch = std::getenv("JNEXT_G46B_WATCH") != nullptr;
        if (g46b_watch) {
            static uint16_t prev_5b8a = 0;
            static uint8_t  prev_5b8e = 0;
            static uint16_t prev_pc   = 0xFFFF;
            static int      watch_logs = 0;
            uint16_t cur_5b8a = static_cast<uint16_t>(
                mem_.read(0x5b8a) | (mem_.read(0x5b8b) << 8));
            uint8_t  cur_5b8e = mem_.read(0x5b8e);
            if (cur_5b8a != prev_5b8a && watch_logs < 60) {
                Log::cpu()->info(
                    "G46B WATCH $5b8a {:#06x}->{:#06x} after PC={:#06x}",
                    prev_5b8a, cur_5b8a, prev_pc);
                ++watch_logs;
            }
            if (cur_5b8e != prev_5b8e && watch_logs < 60) {
                Log::cpu()->info(
                    "G46B WATCH $5b8e {:#04x}->{:#04x} after PC={:#06x}",
                    prev_5b8e, cur_5b8e, prev_pc);
                ++watch_logs;
            }
            prev_5b8a = cur_5b8a;
            prev_5b8e = cur_5b8e;
            prev_pc = pc;
        }
    }
    // Per-instruction trace via Log::cpu_inst() (see log.h). Off by default.
    // Enable with `--log-level cpu_inst=trace` for diagnostic investigations.
    // The PC gate below excludes two NextZXOS firmware RAM-test inner-loops
    // ($0130-$016E pass 1 + $018E-$01CB pass 2) that dominate the trace
    // volume during NextZXOS boot but are not informative. Adjust the gate
    // as needed for other investigations; remove it entirely for un-filtered
    // tracing (very high volume — ~1M lines/sec at 28 MHz).
    if (!((pc >= 0x0130 && pc < 0x016F) || (pc >= 0x018E && pc < 0x01CC))) {
        Log::cpu_inst()->trace("PC={:#06x} op={:#04x}", pc, opcode);
    }
    // G46(b) v2 DIAG: log register + IX + memory state at the sprite-descriptor
    // read site $20E6 so we can see what (IX+$11) maps to. TEMP — remove on
    // G46(b) full closure.
    if (pc == 0x00EF || pc == 0x2043 || pc == 0x20E6) {
        // Log page 0x2F contents (offset 0x18..0x28) — bank 7 high half via NR_57=0x0F
        // helps detect when something wipes our mirror-load.
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            const uint8_t* p2f = mmu->ram().page_ptr(0x2F);
            const uint8_t* p0f = mmu->ram().page_ptr(0x0F);
            if (p2f && p0f) {
                static int cnt = 0;
                if (cnt < 20) {
                    char b2f[80], b0f[80];
                    int n = 0;
                    for (int i = 0; i < 8; ++i) n += std::snprintf(b2f + n, sizeof(b2f) - n, "%02x ", p2f[0x1B + i]);
                    n = 0;
                    for (int i = 0; i < 8; ++i) n += std::snprintf(b0f + n, sizeof(b0f) - n, "%02x ", p0f[0x1B + i]);
                    Log::cpu()->info("PAGES PC={:#06x} p2f[1B:23]={} p0f[1B:23]={}", pc, b2f, b0f);
                    ++cnt;
                }
            }
        }
    }
    // G46(b) v4 ISOLATION TEST: when JNEXT_G46B_PATCH_IX11 is set, restore
    // page 0x2F bytes at the IX position by copying from page 0x0F (= the
    // pre-loaded AltROM-1 upper). Verified empirically that this advances
    // boot past the sprite-descriptor stall to UI rendering (white menu
    // chrome + sprite icon top-left visible at t=14s) but boot still doesn't
    // reach the welcome screen — there are downstream descriptor reads from
    // other locations that this patch doesn't cover. TEMP — diagnostic only,
    // remove on G46(b) full closure.
    if (pc == 0x20E6) {
        static const bool patch_ix11 = std::getenv("JNEXT_G46B_PATCH_IX11") != nullptr;
        if (patch_ix11) {
            uint16_t ix = z80.ix.w;
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                const uint8_t* p0f = mmu->ram().page_ptr(0x0F);
                uint8_t* p2f = mmu->ram().page_ptr(0x2F);
                if (p0f && p2f) {
                    for (int i = 0; i < 32; ++i) {
                        p2f[(ix - 0xE000) + i] = p0f[(ix - 0xE000) + i];
                    }
                }
            }
        }
    }
    if (pc == 0x20E6 || pc == 0x2043 || pc == 0x1DF3) {
        uint16_t ix = z80.ix.w;
        char buf[80];
        int n = 0;
        for (int i = 0; i < 16 && n < 70; ++i) {
            n += std::snprintf(buf + n, sizeof(buf) - n, "%02x ", mem_.read(ix + i));
        }
        char mmu_buf[80] = "n/a";
        char raw_buf[80] = "n/a";
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            int mn = 0;
            for (int s = 0; s < 8 && mn < 70; ++s) {
                mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn, "%02x ", mmu->get_page(s));
            }
            // Direct RAM read of physical SRAM page 0x0F at offset 0x1B onwards
            // (= what the descriptor SHOULD return if MMU correctly mapped slot 7
            // → page 0x0F, OR what it actually contains).
            const uint8_t* p0f = mmu->ram().page_ptr(0x0F);
            const uint8_t* p2f = mmu->ram().page_ptr(0x2F);
            if (p0f) {
                int rn = 0;
                for (int i = 0; i < 8 && rn < 70; ++i) {
                    rn += std::snprintf(raw_buf + rn, sizeof(raw_buf) - rn, "%02x ", p0f[0x1B + i]);
                }
                if (p2f) {
                    rn += std::snprintf(raw_buf + rn, sizeof(raw_buf) - rn, " | p2f: ");
                    for (int i = 0; i < 8 && rn < 70; ++i) {
                        rn += std::snprintf(raw_buf + rn, sizeof(raw_buf) - rn, "%02x ", p2f[0x1B + i]);
                    }
                }
            }
        }
        // Also log NR 0x8C (AltROM control) state.
        uint8_t nr8c = 0;
        bool rom_in_sram = false;
        bool config_mode = false;
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            nr8c = mmu->get_nr_8c();
            rom_in_sram = mmu->rom_in_sram();
            // No public config_mode() getter — read via the cached field via friend or skip
            (void)mmu;
        }
        Log::cpu()->info("G46B SPRITE PC={:#06x} ix={:#06x} sp={:#06x} mmu[0..7]={} nr_8c={:#04x} rom_in_sram={} cfg_mode={}",
                         pc, ix, z80.sp.w, mmu_buf, nr8c, rom_in_sram, config_mode);
        Log::cpu()->info("  via_mem ix[0..15]={}", buf);
        Log::cpu()->info("  raw_p0f[0x1B..0x2A]={}", raw_buf);
    }
    // G46(b) DIAGNOSTIC: when JNEXT_G46B_PATCH is set, force mem[$5b8e] = 0
    // every time PC reaches the exit predicate at enNextZX.rom $279d
    // (LD A,($5b8e) / CP $5c / RET C). With A < $5c the firmware should
    // take the "return-on-current-stack" branch and progress past the
    // stack-switch-back-to-$0000 loop. If boot then advances to BASIC, the
    // divergence point is confirmed and we can hunt the writer that should
    // have computed a lower value. Same patch also hits $2734 (entry
    // stack-switch test) so the entry side also takes the no-switch branch.
    if (pc == 0x279d || pc == 0x2734) {
        static const bool g46b_patch  = std::getenv("JNEXT_G46B_PATCH")  != nullptr;
        static const bool g46b_inject = std::getenv("JNEXT_G46B_INJECT") != nullptr;
        static int patch_hits = 0;
        if (g46b_patch || g46b_inject) {
            uint8_t before = mem_.read(0x5b8e);
            uint16_t sp  = z80.sp.w;
            uint8_t sp0 = mem_.read(sp);
            uint8_t sp1 = mem_.read(static_cast<uint16_t>(sp + 1));
            uint8_t sp2 = mem_.read(static_cast<uint16_t>(sp + 2));
            uint8_t sp3 = mem_.read(static_cast<uint16_t>(sp + 3));
            uint16_t saved_sp_lo = mem_.read(0x5b6a);
            uint16_t saved_sp_hi = mem_.read(0x5b6b);
            uint16_t saved_sp = static_cast<uint16_t>(saved_sp_lo | (saved_sp_hi << 8));
            uint8_t ssp0 = mem_.read(saved_sp);
            uint8_t ssp1 = mem_.read(static_cast<uint16_t>(saved_sp + 1));
            uint8_t ssp2 = mem_.read(static_cast<uint16_t>(saved_sp + 2));
            uint8_t ssp3 = mem_.read(static_cast<uint16_t>(saved_sp + 3));
            // Brute-force test (ONE-SHOT): at the FIRST hit #2 (PC=$279d), inject
            // the real user-stack return chain (`aa 23 74 02` = $23aa then $0274)
            // into the user stack so that the natural stack-switch-back / RET
            // path will pop $23aa instead of the zeros currently present. After
            // the first injection, leave the user stack alone — subsequent
            // iterations should reveal whether the firmware progresses past the
            // loop or comes right back. Re-firing every iteration corrupts
            // newer return chains.
            static bool injected_once = false;
            if (g46b_inject && pc == 0x279d && !injected_once) {
                mem_.write(saved_sp,                                    0xaa);
                mem_.write(static_cast<uint16_t>(saved_sp + 1),         0x23);
                mem_.write(static_cast<uint16_t>(saved_sp + 2),         0x74);
                mem_.write(static_cast<uint16_t>(saved_sp + 3),         0x02);
                injected_once = true;
            }
            // $5b8e patch only when JNEXT_G46B_PATCH is set; without it, firmware
            // takes the natural stack-switch-back path and the inject above
            // determines what gets popped.
            if (g46b_patch) {
                mem_.write(0x5b8e, 0x00);
            }
            if (++patch_hits <= 12) {
                // Step 1 add-on: also dump the slot 6/7 bank mapping at the
                // hit moment, so we can tell if slot 7's mapping is the SAME
                // at hit #1 ($2734) and hit #2 ($279d). If yes, the divergence
                // is content-corruption (someone wrote 0 to slot-7 RAM during
                // the iteration body). If no, the firmware itself is paging
                // differently. Since `mem_` is MemoryInterface, downcast to
                // Mmu* (the only real implementation) to access slot state.
                int slot7_nr = -1, slot7_eff = -1;
                if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                    slot7_nr  = mmu->get_page(7);            // NR 0x57 cached value
                    slot7_eff = mmu->get_effective_page(7);  // resolves through legacy paging
                }
                // G46(b) STEP 2 add-on: simulate the firmware's exact port-read
                // sequence (OUT $243B,#57 ; IN A,($253B)) to capture what the
                // firmware would actually observe at this PC. This bypasses any
                // emulator-side cache (Mmu::get_page) and goes through the
                // canonical NextReg::read_selected() path. Must save+restore the
                // currently-selected NR (selected_) so the firmware's own NR
                // selection state is not disturbed.
                //
                // We probe the NR-port path with two reads:
                //  - port_nr57: simulated IN A,($253B) after OUT $243B,#57
                //               → returns NextReg::regs_[0x57] (no read handler
                //                 is registered for 0x57; see emulator.cpp 1281)
                //  - we also re-read after NOT doing the select, to confirm
                //    the previous selected_ has been restored.
                uint8_t port_nr57    = 0xAA;
                uint8_t prev_sel_57  = 0xAA;  // selected NR at hit time
                {
                    // Save the current selected_ via a port-read of NR_06
                    // (any harmless reg) is overkill — instead use a side
                    // channel: read $243B is invalid (no read handler), so
                    // we have to track via OUT/IN sequence. We avoid that
                    // complexity by restoring via a final OUT $243B,<orig>.
                    //
                    // Read what's currently selected before we touch it:
                    // there's no read port for 'selected_' itself, so we
                    // capture by reading $253B FIRST (this gives the value
                    // of whatever NR is currently selected, which we don't
                    // care about) — but then we must remember which NR
                    // *was* selected so we can restore it. The cleanest
                    // approach is to peek at NextReg state directly, but
                    // we don't have a NextReg* here. Pragmatic compromise:
                    // probe NR_57, then restore selection to 0x57 (a
                    // common firmware selection that's harmless). For a
                    // bigger trace this is acceptable; if the firmware was
                    // mid-sequence on a different NR, only the diagnostic
                    // window (this hit) would see the change.
                    io_.out(0x243B, 0x57);
                    port_nr57 = io_.in(0x253B);
                    // Note: we LEAVE selected_ = 0x57 after this. That is
                    // safe for the diagnostic because: (a) firmware always
                    // re-selects before reading any NR, and (b) the next
                    // firmware read will OUT $243B,<wanted> first per
                    // canonical TBBlue idiom.
                    (void)prev_sel_57;
                }
                Log::cpu()->info(
                    "G46B HOOK #{} PC={:#06x} mem[$5b8e]={:#04x}{} | "
                    "SP={:#06x} stack=[{:02x} {:02x} {:02x} {:02x}] | "
                    "saved_SP@5b6a={:#06x} stack=[{:02x} {:02x} {:02x} {:02x}] | "
                    "slot7 NR_57(mmu_cached)={:#04x} effective={:#04x} "
                    "port_NR57(via $253B)={:#04x}{}",
                    patch_hits, pc, before, (g46b_patch ? "->0x00" : ""),
                    sp, sp0, sp1, sp2, sp3,
                    saved_sp, ssp0, ssp1, ssp2, ssp3,
                    slot7_nr, slot7_eff, port_nr57,
                    (g46b_inject && pc == 0x279d) ? " [INJECT aa 23 74 02 @ saved_SP]" : "");
            }
        }
    }

    if (opcode == 0xED) {
        uint8_t ext = mem_.read(static_cast<uint16_t>(pc + 1));

        // Magic breakpoint: ED FF (ZEsarUX/Spectaculator convention)
        if (ext == 0xFF && on_magic_breakpoint) {
            if (on_magic_breakpoint(pc)) {
                // Advance PC past ED FF (acts as 2-byte NOP)
                z80.pc.w = (pc + 2) & 0xFFFF;
                sync_regs_from_fuse(regs_);
                return 8;  // 8 T-states like a NOP
            }
        }

        if (kZ80NOpcodeTable[ext]) {
            Log::cpu()->trace("Z80N opcode ED {:#04x} at PC={:#06x}", ext, pc);
            // G87: fire M1 callback on BOTH bytes of the ED-prefix opcode so
            // the IM2 RETI/RETN/IM-mode decoder FSM (im2_control.vhd:158-209)
            // advances S_0 → S_ED_T4 → ... — the FSM models per-fetched-byte
            // T4 events and was previously starved when only the ED byte was
            // delivered.
            if (on_m1_cycle) on_m1_cycle(pc, opcode);
            if (on_m1_cycle) on_m1_cycle(static_cast<uint16_t>((pc + 1) & 0xFFFF), ext);

            // Advance PC past ED + ext byte; execute_z80n reads any operands
            z80.pc.w = (pc + 2) & 0xFFFF;
            // Increment R by 2: one for ED prefix M1, one for ext byte M1
            z80.r = (z80.r + 2) & 0x7F;
            sync_regs_from_fuse(regs_);

            int t = execute_z80n(ext, *this);
            if (t < 0) t = 8;

            sync_fuse_from_regs(regs_);
            return t;
        }

        // G87: non-Z80N ED instruction (RETI = ED 4D, RETN = ED 45, IM 0/1/2,
        // LDIR/CPIR/INI/OUTI/etc., MUL/SBC HL,rr, etc. — handled by FUSE).
        // Fire BOTH M1 callbacks BEFORE fuse_z80_execute_one() so the IM2
        // decoder FSM gets the ED + ext byte sequence it needs to detect
        // RETI/RETN and surface reti_seen / retn_seen pulses.
        //
        // Out of scope for this wave: DD/FD prefix and CB prefix paths still
        // deliver only the prefix byte to on_m1_cycle. The im2 FSM has
        // S_DDFD_T4 and S_CB_T4 states that would benefit from per-byte
        // delivery here too, but the SKIP rows targeted by G87 are RETI/RETN
        // only — DD/FD/CB tracking remains a follow-up.
        if (on_m1_cycle) on_m1_cycle(pc, opcode);
        if (on_m1_cycle) on_m1_cycle(static_cast<uint16_t>((pc + 1) & 0xFFFF), ext);
        int cycles_ed = fuse_z80_execute_one();
        sync_regs_from_fuse(regs_);
        return (cycles_ed > 0) ? cycles_ed : 4;
    }

    // Magic breakpoint: DD 01 (CSpect convention)
    if (opcode == 0xDD) {
        uint8_t ext = mem_.read(static_cast<uint16_t>(pc + 1));
        if (ext == 0x01 && on_magic_breakpoint) {
            if (on_magic_breakpoint(pc)) {
                // Advance PC past DD 01 (acts as 2-byte NOP)
                z80.pc.w = (pc + 2) & 0xFFFF;
                sync_regs_from_fuse(regs_);
                return 8;
            }
        }
    }

    // ── Normal Z80 instruction ─────────────────────────────────────────
    if (on_m1_cycle) on_m1_cycle(pc, opcode);

    int cycles = fuse_z80_execute_one();

    sync_regs_from_fuse(regs_);
    return (cycles > 0) ? cycles : 4;
}

void Z80Cpu::request_interrupt(uint8_t vector) {
    int_pending_ = true;
    int_vector_  = vector;
    int_requested_at_ = *fuse_z80_tstates_ptr();
}

void Z80Cpu::request_nmi() {
    nmi_pending_ = true;
}

void Z80Cpu::save_state(StateWriter& w) const
{
    // Registers
    w.write_u16(regs_.AF);  w.write_u16(regs_.BC);
    w.write_u16(regs_.DE);  w.write_u16(regs_.HL);
    w.write_u16(regs_.AF2); w.write_u16(regs_.BC2);
    w.write_u16(regs_.DE2); w.write_u16(regs_.HL2);
    w.write_u16(regs_.IX);  w.write_u16(regs_.IY);
    w.write_u16(regs_.SP);  w.write_u16(regs_.PC);
    w.write_u8(regs_.I);    w.write_u8(regs_.R);
    w.write_u8(regs_.IFF1); w.write_u8(regs_.IFF2);
    w.write_u8(regs_.IM);
    w.write_bool(regs_.halted);
    // Interrupt state
    w.write_bool(nmi_pending_);
    w.write_bool(int_pending_);
    w.write_u8(int_vector_);
    w.write_u32(int_requested_at_);
}

void Z80Cpu::load_state(StateReader& r)
{
    regs_.AF  = r.read_u16(); regs_.BC  = r.read_u16();
    regs_.DE  = r.read_u16(); regs_.HL  = r.read_u16();
    regs_.AF2 = r.read_u16(); regs_.BC2 = r.read_u16();
    regs_.DE2 = r.read_u16(); regs_.HL2 = r.read_u16();
    regs_.IX  = r.read_u16(); regs_.IY  = r.read_u16();
    regs_.SP  = r.read_u16(); regs_.PC  = r.read_u16();
    regs_.I   = r.read_u8();  regs_.R   = r.read_u8();
    regs_.IFF1 = r.read_u8(); regs_.IFF2 = r.read_u8();
    regs_.IM  = r.read_u8();
    regs_.halted = r.read_bool();
    nmi_pending_ = r.read_bool();
    int_pending_ = r.read_bool();
    int_vector_  = r.read_u8();
    int_requested_at_ = r.read_u32();
    // FUSE global z80 struct is synced on the next execute() call via
    // sync_fuse_from_regs(regs_) — no explicit sync needed here.
}

// ══════════════════════════════════════════════════════════════════════════
// Contention runtime — wires ContentionModel::contention_tick() into the
// FUSE Z80 read/write/in/out callbacks for VHDL-faithful per-cycle
// contention. Phase-2 wiring 2026-04-26 (TASK-CONTENTION-MODEL-PLAN).
// ══════════════════════════════════════════════════════════════════════════

#include "core/emulator_config.h"

// G53 (2026-05-01) — z80_build_contention_tables() and
// z80_set_page_contended() are retired. The FUSE in-opcode contention
// path now goes through ContentionModel::contention_tick() via the
// CORETEST function overrides at the top of this file (G141). No
// callers remain in the codebase.

void z80_set_contention_runtime(ContentionModel* cm, Mmu* mmu, MachineType machine_type)
{
    s_contention     = cm;
    s_contention_mmu = mmu;
    const auto t = machine_timing(machine_type);
    s_tstates_per_line  = t.tstates_per_line;
    s_tstates_per_frame = t.tstates_per_frame;
}
