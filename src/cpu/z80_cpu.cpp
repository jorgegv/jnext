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

/// VHDL `mem_active_page` — the MMU<i> register value for the 8K slot
/// the CPU bus is currently addressing. Per zxnext.vhd:2949-2956,
///   mem_active_page <= MMU0 when cpu_a(15:13) = "000" else
///                      MMU1 when cpu_a(15:13) = "001" else
///                      ... ;
/// i.e. the SRAM page resolution uses the LIVE MMU<i> register value
/// (= NR 0x50..0x57 visible value, mirrored into Mmu::nr_mmu_[]). For
/// slots in legacy-ROM auto-paging (NR $50/$51 = 0xFF sentinel) the
/// register holds 0xFF, NOT the sram_rom-derived physical ROM page —
/// this is what the contention gate at zxnext.vhd:4489
///   mem_contend = '0' when mem_active_page(7:4) /= "0000"
/// keys on, so a legacy-ROM slot 0/1 access never contends regardless
/// of which physical ROM page is selected.
///
/// Verify6-memory class-(a) fix: previously this used
/// `Mmu::get_effective_page` which falls through to `slots_[slot]` (the
/// physical SRAM page) when nr_mmu_[slot]==0xFF. For 128K with
/// sram_rom=1 (slot 0 → physical ROM page 2) or +3 with sram_rom>=2
/// (slot 0/1 → pages 4..7), the resulting low nibble carried bit-1
/// (128K) or bit-3 (+3) set, causing the contention model to falsely
/// fire on slot-0/1 ROM accesses. VHDL keys mem_active_page on MMU<i>,
/// so the 0xFF sentinel suppresses contention as expected.
inline uint8_t mem_active_page_for(uint16_t address) {
    if (!s_contention_mmu) return 0xFF;
    int slot = address >> 13;
    return s_contention_mmu->get_page(slot);
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
    //
    // VHDL zxnext.vhd:2033 (pulse_count_end formula):
    //   pulse_count_end <= pulse_count(5) and (machine_timing_48 or
    //                       machine_timing_p3 or pulse_count(2));
    // → 48K/+3 release /INT after 32 cycles (bit 5 alone fires the gate);
    //   128K/Pentagon/Next-default need bit 5 AND bit 2 → 36 cycles.
    // machine_48_or_p3_ is set by Emulator from the active MachineType and
    // is also re-fanned-out from runtime NR 0x03 machine_timing writes.
    const uint32_t int_pulse_tstates = machine_48_or_p3_ ? 32u : 36u;
    if (int_pending_) {
        if (tstates - int_requested_at_ > int_pulse_tstates && !z80.iff1) {
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

            // Pass-5 fix: VHDL-faithful M1 contention for Z80N opcodes.
            // Mirrors FUSE's `contend_read(PC, 4)` per M1 fetch (see
            // opcodes_base.c:1075 for ED prefix; the inner 0xed switch then
            // does the same for the ext byte). Each contend_read() adds the
            // VHDL contention stretch keyed on (hc, vc) at the current
            // tstates position, then advances tstates by 4 (the M1 length).
            // Without this, every Z80N opcode bypassed the M1 contention
            // window — the supervisor uses NEXTREG/MUL/ADD HL/DE/BC,A
            // constantly, so contended pages running during the active raster
            // window would skip 6/5/4/3/2/1 T-states per Z80N instruction.
            // Class-(a) per pass-5 quantification (boot supervisor cycles
            // ~2.4M Z80N opcodes per second; even 1 missed cycle on 10 % of
            // those is 240 K T-states/s, ≈3.4 % timing drift).
            //
            // The two contend_read() calls also bake the M1 contention into
            // the FUSE `tstates` clock so subsequent fuse_z80_readbyte() /
            // contend_read() calls (post-Z80N) derive (hc, vc) from a
            // contention-correct frame position.
            //
            // Note: the per-call 4 T-states subsume what was previously
            // supplied by `tstates += t` (where t already included the 8 T
            // baseline). We therefore subtract 8 from the post-instruction
            // tstates increment below. For Z80N opcodes that read operand
            // bytes (TEST_N, ADD_*_NN, NEXTREG_NN, NEXTREG_A, PUSH_NN), the
            // operand read contention is still NOT modelled — flagged
            // class-(b) for a future pass that routes operand accesses
            // through fuse_z80_readbyte (which would also push the +3 T per
            // operand into tstates and require further T-state arithmetic).
            //
            // Snapshot tstates BEFORE the contend_read pair so we can return
            // the FULL elapsed delta (including any contention stretch) at
            // the bottom of the branch — matching FUSE's
            // fuse_z80_execute_one() return shape (line 213).
            libspectrum_dword start_ts = tstates;
            contend_read(pc, 4);
            contend_read(static_cast<uint16_t>((pc + 1) & 0xFFFF), 4);

            // Advance PC past ED + ext byte; execute_z80n reads any operands
            z80.pc.w = (pc + 2) & 0xFFFF;
            // Increment R by 2: one for ED prefix M1, one for ext byte M1
            z80.r = (z80.r + 2) & 0x7F;
            // Pass-4 fix: mirror fuse_z80_execute_one() pre-dispatch state
            // hygiene — Q=0 and iff2_read=0 at the start of every opcode.
            //
            // Q semantics (FUSE convention, used by SCF/CCF X/Y composition):
            // every opcode begins with Q=0; F-writing opcodes set Q=F at end.
            // The very next SCF/CCF reads `last_Q = Q` from this trail and
            // computes X/Y as (last_Q ^ F) | A. Z80N opcodes that DO NOT
            // write F (SWAPNIB, MIRROR_A, MUL_DE, BSLA/BSRA/BSRL/BSRF/BRLC,
            // ADD_*_NN, PUSH_NN, OUTINB, NEXTREG_NN/A, PIXELDN/AD, SETAE,
            // JP_C, LDPIRX) MUST leave Q=0 so the next SCF/CCF behaves like
            // it's coming after a non-F-writing standard opcode (LD r,r' /
            // JP / etc.). Pre-fix: Q persisted from the prior FUSE opcode,
            // poisoning SCF/CCF X/Y bits across any Z80N → SCF sequence.
            //
            // iff2_read semantics (NMOS LD A,I / LD A,R quirk): if INT is
            // accepted on the very next instruction boundary after LD A,I/R
            // and IFF2 was 1, the P flag (which the LD just stored) gets
            // cleared to model the well-known race condition. FUSE clears
            // iff2_read=0 at the top of every opcode. A Z80N opcode in
            // between LD A,I and an INT acceptance is a non-immediate
            // boundary, so iff2_read MUST be cleared too — otherwise jnext
            // fires the NMOS quirk wrongly.
            z80.q = 0;
            z80.iff2_read = 0;
            sync_regs_from_fuse(regs_);

            int t = execute_z80n(ext, *this);
            if (t < 0) t = 8;

            // VHDL-faithful timekeeping: Z80N opcodes bypass FUSE's
            // fuse_z80_execute_one() entirely (FUSE doesn't decode them).
            //
            // Pass-5: the two contend_read(pc, 4) / contend_read(pc+1, 4)
            // calls above add 8 T-states for the M1 fetches AND any
            // contention stretch the (hc, vc) window emits.
            //
            // Pass-6: execute_z80n() now self-manages the FULL post-M1
            // tstates advance — operand/data reads via fuse_z80_readbyte
            // and writes via fuse_z80_writebyte (each adds 3 T + contention
            // stretch); real port I/O (OUTINB, JP_C) via
            // fuse_z80_writeport / fuse_z80_readport (each 4 T + contention);
            // and any leftover internal idle cycles via raw `tstates +=`.
            // The wrapper therefore does NOT add (t - 8) — that would
            // double-count the post-M1 portion, since execute_z80n() has
            // already advanced tstates by the data-access + internal time.
            //
            // The returned `t` value is now purely informational — it is
            // the spec-total instruction T-state count (8 M1 + post-M1).
            // We still snapshot start_ts before the contend_read pair so
            // the wrapper returns the FULL elapsed delta (8 M1 + M1
            // contention + data-access T + data-access contention +
            // internal idle), matching FUSE's fuse_z80_execute_one()
            // return shape.
            (void)t;  // value unused after Pass-6 — self-managed inside

            sync_fuse_from_regs(regs_);
            return static_cast<int>(tstates - start_ts);
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
    // Pass-3 fix: include the hidden WZ/MEMPTR register and the F-assembly
    // shadow Q. Without these, save/load loses the undocumented X/Y flag
    // composition state used by BIT (HL)/(IX+d)/(IY+d) and by SCF/CCF
    // (FUSE consults `last_Q = z80.q` at the start of every opcode and
    // BIT_MEMPTR uses `z80.memptr.b.h` for X/Y composition). Both fields
    // are already present in Z80Registers and synced by sync_*regs.
    w.write_u16(regs_.MEMPTR);
    w.write_u8(regs_.Q);
    // Pass-4 fix: persist FUSE-internal interrupt-related state that lives
    // ONLY in the global `z80` struct and is not mirrored into Z80Registers.
    //   - interrupts_enabled_at: signed_dword tstate stamp set by EI; gates
    //     fuse_z80_interrupt() on the immediately-following instruction
    //     (VHDL t80n.vhd EI semantics — IFF1 takes effect AFTER the next
    //     opcode boundary, mirroring real-hardware NMI/INT acceptance
    //     window). Without persisting this across save/load, a snapshot
    //     taken on the cycle immediately after EI would let an INT fire
    //     one instruction earlier than spec when restored.
    //   - iff2_read: NMOS LD A,I / LD A,R quirk flag — if INT is accepted
    //     on the next instruction boundary, the P flag (which holds IFF2
    //     stored by the LD) gets cleared. Without persistence, a snapshot
    //     taken between LD A,I and an immediately-pending INT would skip
    //     the quirk on restore.
    w.write_i32(z80.interrupts_enabled_at);
    w.write_u8(static_cast<uint8_t>(z80.iff2_read ? 1 : 0));
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
    // Pass-3 fix: restore MEMPTR + Q (see save_state comment).
    regs_.MEMPTR = r.read_u16();
    regs_.Q      = r.read_u8();
    // Pass-4 fix: restore FUSE-internal interrupts_enabled_at + iff2_read.
    // Push directly into the global z80 struct; sync_fuse_from_regs() does
    // NOT touch these fields (they have no Z80Registers mirror).
    z80.interrupts_enabled_at = static_cast<libspectrum_signed_dword>(r.read_i32());
    z80.iff2_read = (r.read_u8() != 0) ? 1 : 0;
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
