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
#include "peripheral/divmmc.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

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

    // G46(b) Probe 7 (TEMP — remove on G46(b) closure): ring buffer of last
    // N PCs for caller-trace dumps. Updated every CPU step.
    static constexpr int G46B_PC_RING_SIZE = 256;
    static uint16_t g46b_pc_ring[G46B_PC_RING_SIZE] = {};
    static int g46b_pc_ring_head = 0;
    g46b_pc_ring[g46b_pc_ring_head] = pc;
    g46b_pc_ring_head = (g46b_pc_ring_head + 1) % G46B_PC_RING_SIZE;

    // G46(b) Probe 15 (TEMP — remove on G46(b) closure): IX-write tracer.
    // Log every PC where IX changes between consecutive M1 fetches, with
    // the new IX value. Stops after FIRST PC=0x1800 hit so we can see the
    // upstream IX-loading chain in finite output.
    static uint16_t g46b_ix_prev = 0xDEAD;
    static int g46b_ix_log_count = 0;
    static bool g46b_ix_done = false;
    if (!g46b_ix_done && z80.ix.w != g46b_ix_prev) {
        if (g46b_ix_log_count < 200) {
            char mmu_buf[80] = "";
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                int mn = 0;
                for (int s = 0; s < 8 && mn < 70; ++s) {
                    mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn,
                                        "%02x ", mmu->get_page(s));
                }
            }
            int prev_idx = (g46b_pc_ring_head - 2 + G46B_PC_RING_SIZE)
                           % G46B_PC_RING_SIZE;
            uint16_t writer_pc = g46b_pc_ring[prev_idx];
            char wb[24];
            std::snprintf(wb, sizeof(wb), "%02x %02x %02x %02x",
                          mem_.read(writer_pc),
                          mem_.read(writer_pc + 1),
                          mem_.read(writer_pc + 2),
                          mem_.read(writer_pc + 3));
            Log::cpu()->info(
                "G46B IX-WRITE writer_pc={:#06x} bytes='{}' new_ix={:#06x} "
                "(prev={:#06x}) cur_pc={:#06x} mmu={}",
                writer_pc, wb, z80.ix.w, g46b_ix_prev, pc, mmu_buf);
            ++g46b_ix_log_count;
        }
        g46b_ix_prev = z80.ix.w;
    }

    // G46(b) Probe 16 (TEMP): control-flow tracer between IX=$E01B set
    // and first PC=0x1800. Log only PCs that are the target of CALL/JP/
    // JR/RET AND have not been visited before. Goal: get the unique
    // code path between the bank-2 sprite-prep routine and PC=0x1800.
    static bool g46b_cf_active = false;
    static int g46b_cf_count = 0;
    static uint8_t g46b_pc_seen[0x10000 / 8] = {};  // bitmap, 1 bit per PC
    if (!g46b_ix_done && z80.ix.w == 0xE01B && !g46b_cf_active) {
        g46b_cf_active = true;
        std::memset(g46b_pc_seen, 0, sizeof(g46b_pc_seen));
        Log::cpu()->info(
            "G46B CF-TRACE START at PC={:#06x}", pc);
    }
    if (g46b_cf_active && !g46b_ix_done && g46b_cf_count < 600) {
        int prev_idx = (g46b_pc_ring_head - 2 + G46B_PC_RING_SIZE)
                       % G46B_PC_RING_SIZE;
        uint16_t prev_pc = g46b_pc_ring[prev_idx];
        uint8_t prev_b0 = mem_.read(prev_pc);
        bool is_cf = false;
        if (prev_b0 == 0xC3 || prev_b0 == 0xCD || prev_b0 == 0xC9 ||
            (prev_b0 >= 0xC0 && prev_b0 <= 0xF8 && (prev_b0 & 7) == 0) ||
            (prev_b0 >= 0xC2 && prev_b0 <= 0xFA && (prev_b0 & 7) == 2) ||
            (prev_b0 >= 0xC4 && prev_b0 <= 0xFC && (prev_b0 & 7) == 4) ||
            prev_b0 == 0x18 || prev_b0 == 0x10 ||
            prev_b0 == 0x20 || prev_b0 == 0x28 ||
            prev_b0 == 0x30 || prev_b0 == 0x38 ||
            prev_b0 == 0xE9) {
            is_cf = true;
        }
        if (is_cf) {
            uint16_t pc16 = pc;
            uint8_t bit = 1u << (pc16 & 7);
            uint16_t byte_idx = pc16 >> 3;
            if (!(g46b_pc_seen[byte_idx] & bit)) {
                g46b_pc_seen[byte_idx] |= bit;
                Log::cpu()->info(
                    "G46B CF dst={:#06x} from {:#06x} op={:#04x} ix={:#06x}",
                    pc, prev_pc, prev_b0, z80.ix.w);
                ++g46b_cf_count;
            }
        }
    }

    // Stop at first PC=0x20E6 (the sprite stall — final divergence point)
    // so we capture the supervisor's full path from IX=$E01B set up to
    // the consumer that fails.
    if (!g46b_ix_done && pc == 0x20E6) {
        g46b_ix_done = true;
        Log::cpu()->info(
            "G46B IX-WRITE TRACE STOP at first PC=0x20E6 (ix={:#06x})",
            z80.ix.w);
    }

    // G46(b) Probe 22 (TEMP): fire on FIRST entry to PC range $3C00-$3CFF
    // (= the entry into the NOP sled before AUTOMAP fires). This lands
    // us at the JP/CALL/RET that ENTERED the sled territory, just BEFORE
    // the NOP run-up. Stack TOS at this point is the AUTOMAP-sled return
    // target.
    static bool g46b_p22_done = false;
    static uint16_t g46b_p22_prev_pc = 0;
    if (!g46b_p22_done && pc >= 0x3000 && pc <= 0x3FFF
        && (g46b_p22_prev_pc < 0x3000 || g46b_p22_prev_pc > 0x3FFF)) {
        g46b_p22_done = true;
        char stack_buf[256] = "";
        int sn = 0;
        for (int i = 0; i < 16 && sn < 240; ++i) {
            uint16_t word = static_cast<uint16_t>(
                mem_.read(z80.sp.w + i*2) |
                (mem_.read(z80.sp.w + i*2 + 1) << 8));
            sn += std::snprintf(stack_buf + sn, sizeof(stack_buf) - sn,
                                "%04x ", word);
        }
        Log::cpu()->info(
            "G46B P22 EARLY PC={:#06x} SP={:#06x} stack=[{}]",
            pc, z80.sp.w, stack_buf);
        char ring_buf[400];
        int rn = 0;
        for (int i = 0; i < G46B_PC_RING_SIZE && rn < 380; ++i) {
            int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
            rn += std::snprintf(ring_buf + rn,
                                sizeof(ring_buf) - rn,
                                "%04x ", g46b_pc_ring[idx]);
        }
        Log::cpu()->info("G46B P22 ring: {}", ring_buf);
    }
    g46b_p22_prev_pc = pc;

    // G46(b) Probe 23 (TEMP — remove on G46(b) closure): SP-tracer for
    // CALL/RET/RST/RETI/RETN. Logs SP_before/SP_after/PC_target/landed_PC
    // for each control-flow event and tracks a running call-depth counter.
    //
    // Mechanism: per-step hook fires BEFORE fuse_z80_execute_one(). On each
    // entry we (1) flush the PREVIOUS step's pending event with the now-known
    // SP_after / landed_PC, then (2) classify the CURRENT instruction and
    // arm a new pending event if it is a CF op.
    //
    // Output: /tmp/g46b-sp-trace.log, capped at 8000 events (configurable).
    // Untaken conditional CALL/RET produce a delta=0 entry tagged ".nt"
    // (not taken) so we can see them.
    {
        constexpr int G46B_SP_TRACE_MAX = 8000;
        static FILE* g46b_sp_fp = nullptr;
        static int   g46b_sp_count = 0;
        static int   g46b_sp_depth = 0;        // running CALL/RET depth
        static int   g46b_sp_min_depth = 0;
        static int   g46b_sp_max_depth = 0;
        static bool  g46b_sp_pending = false;
        static uint16_t    g46b_sp_pre_pc = 0;
        static uint16_t    g46b_sp_pre_sp = 0;
        static uint8_t     g46b_sp_pre_op = 0;
        static uint8_t     g46b_sp_pre_op2 = 0;
        static uint16_t    g46b_sp_pre_target = 0;
        static const char* g46b_sp_pre_kind = "";

        if (!g46b_sp_fp && g46b_sp_count == 0) {
            g46b_sp_fp = std::fopen("/tmp/g46b-sp-trace.log", "w");
            if (g46b_sp_fp) {
                std::fprintf(g46b_sp_fp,
                    "# idx kind pc op sp_before sp_after delta landed_pc"
                    " target depth\n");
                std::fflush(g46b_sp_fp);  // header survives SIGKILL
            }
        }
        // Flush every 256 events so partial trace survives SIGKILL.
        if (g46b_sp_fp && g46b_sp_count > 0
            && (g46b_sp_count & 0xFF) == 0
            && !g46b_sp_pending) {
            std::fflush(g46b_sp_fp);
        }

        // (1) Flush pending event from PREVIOUS step.
        if (g46b_sp_fp && g46b_sp_pending && g46b_sp_count < G46B_SP_TRACE_MAX) {
            int delta = static_cast<int>(static_cast<int16_t>(
                z80.sp.w - g46b_sp_pre_sp));
            // Update depth from delta (stack grows DOWN: -2 = push, +2 = pop)
            if (delta == -2) {
                ++g46b_sp_depth;
                if (g46b_sp_depth > g46b_sp_max_depth) g46b_sp_max_depth = g46b_sp_depth;
            } else if (delta == +2) {
                --g46b_sp_depth;
                if (g46b_sp_depth < g46b_sp_min_depth) g46b_sp_min_depth = g46b_sp_depth;
            }
            const char* kind = g46b_sp_pre_kind;
            char tag_buf[16];
            if (delta == 0 && (kind[0] == 'C' || kind[0] == 'R')) {
                std::snprintf(tag_buf, sizeof(tag_buf), "%s.nt", kind);
                kind = tag_buf;
            }
            std::fprintf(g46b_sp_fp,
                "%6d %-9s pc=$%04x op=$%02x%02x sp_b=$%04x sp_a=$%04x"
                " d=%+d landed=$%04x tgt=$%04x depth=%d\n",
                g46b_sp_count, kind, g46b_sp_pre_pc,
                g46b_sp_pre_op, g46b_sp_pre_op2,
                g46b_sp_pre_sp, z80.sp.w, delta, pc, g46b_sp_pre_target,
                g46b_sp_depth);
            // G46(b) Probe 24-lite: at every event log mem[$5BFC..$5C03]
            // (the 8 bytes of stack near the AUTOMAP-sled $3D00 RET pops).
            // Uses Mmu::peek() (NOT mem_.read()) to skip the p3_floating_
            // bus_dat_ latch side effect — mem_.read() at contended slots
            // perturbs emulator state and changed boot path in earlier
            // attempts. peek() bypasses that latch.
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                std::fprintf(g46b_sp_fp,
                    "        mem[5bfc..5c03]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    mmu->peek(0x5BFC), mmu->peek(0x5BFD), mmu->peek(0x5BFE),
                    mmu->peek(0x5BFF), mmu->peek(0x5C00), mmu->peek(0x5C01),
                    mmu->peek(0x5C02), mmu->peek(0x5C03));
            }
            if (g46b_sp_count == G46B_SP_TRACE_MAX) {
                std::fprintf(g46b_sp_fp,
                    "# CAP REACHED. min_depth=%d max_depth=%d\n",
                    g46b_sp_min_depth, g46b_sp_max_depth);
                std::fflush(g46b_sp_fp);
                std::fclose(g46b_sp_fp);
                g46b_sp_fp = nullptr;
            }
            g46b_sp_pending = false;
        }

        // (2) Classify CURRENT instruction. Arm pending event if CF op.
        // Uses Mmu::peek() (NOT mem_.read()) so opcode-byte inspection
        // here doesn't perturb p3_floating_bus_dat_ on contended slots.
        if (g46b_sp_fp && g46b_sp_count < G46B_SP_TRACE_MAX) {
            static Mmu* mmu_for_peek = dynamic_cast<Mmu*>(&mem_);
            static bool g46b_cast_logged = false;
            if (!g46b_cast_logged) {
                uint8_t b0 = mmu_for_peek ? mmu_for_peek->peek(pc) : 0xAA;
                uint8_t b1 = mmu_for_peek ? mmu_for_peek->peek(static_cast<uint16_t>(pc + 1)) : 0xAA;
                Log::cpu()->info(
                    "G46B SP-tracer: dynamic_cast Mmu* = {} pc={:#06x} peek={:#04x},{:#04x}",
                    (void*)mmu_for_peek, pc, b0, b1);
                g46b_cast_logged = true;
            }
            auto rd = [&](uint16_t a) -> uint8_t {
                return mmu_for_peek ? mmu_for_peek->peek(a) : 0;
            };
            uint8_t op  = rd(pc);
            uint8_t op2 = rd(static_cast<uint16_t>(pc + 1));
            const char* kind = nullptr;
            uint16_t target = 0;
            if (op == 0xCD) {
                kind = "CALL";
                target = static_cast<uint16_t>(
                    op2 | (rd(static_cast<uint16_t>(pc + 2)) << 8));
            } else if ((op & 0xC7) == 0xC4) {
                kind = "CALLcc";
                target = static_cast<uint16_t>(
                    op2 | (rd(static_cast<uint16_t>(pc + 2)) << 8));
            } else if (op == 0xC9) {
                kind = "RET";
            } else if ((op & 0xC7) == 0xC0) {
                kind = "RETcc";
            } else if ((op & 0xC7) == 0xC7) {
                kind = "RST";
                target = static_cast<uint16_t>(op & 0x38);
            } else if (op == 0xED) {
                // RETN family: ED 45/55/5D/65/6D/75/7D
                // RETI       : ED 4D
                if (op2 == 0x4D) {
                    kind = "RETI";
                } else if (op2 == 0x45 || op2 == 0x55 || op2 == 0x5D
                           || op2 == 0x65 || op2 == 0x6D || op2 == 0x75
                           || op2 == 0x7D) {
                    kind = "RETN";
                } else if (op2 == 0x8A) {
                    // Z80N PUSH nn (Z80NOpcode::PUSH_NN)
                    kind = "Z80N_PUSH";
                    target = static_cast<uint16_t>(
                        (rd(static_cast<uint16_t>(pc + 2)) << 8) |
                        rd(static_cast<uint16_t>(pc + 3)));
                }
            } else if (op == 0xC5) {
                kind = "PUSH_BC";
                target = z80.bc.w;
            } else if (op == 0xD5) {
                kind = "PUSH_DE";
                target = z80.de.w;
            } else if (op == 0xE5) {
                kind = "PUSH_HL";
                target = z80.hl.w;
            } else if (op == 0xF5) {
                kind = "PUSH_AF";
                target = z80.af.w;
            } else if (op == 0xC1) {
                kind = "POP_BC";
            } else if (op == 0xD1) {
                kind = "POP_DE";
            } else if (op == 0xE1) {
                kind = "POP_HL";
            } else if (op == 0xF1) {
                kind = "POP_AF";
            } else if ((op == 0xDD || op == 0xFD) && op2 == 0xE5) {
                kind = (op == 0xDD) ? "PUSH_IX" : "PUSH_IY";
                target = (op == 0xDD) ? z80.ix.w : z80.iy.w;
            } else if ((op == 0xDD || op == 0xFD) && op2 == 0xE1) {
                kind = (op == 0xDD) ? "POP_IX" : "POP_IY";
            }
            if (kind) {
                g46b_sp_pre_pc     = pc;
                g46b_sp_pre_sp     = z80.sp.w;
                g46b_sp_pre_op     = op;
                g46b_sp_pre_op2    = op2;
                g46b_sp_pre_target = target;
                g46b_sp_pre_kind   = kind;
                g46b_sp_pending    = true;
            }
        }
    }

    // G46(b) Probe 24 (TEMP — remove on G46(b) closure): write-watch on
    // RAM addresses $5BFE-$5C02. On each step, peek() the watched bytes
    // (side-effect-free — does NOT clobber p3_floating_bus_dat_) and
    // log any change with the writer PC (= ring buffer entry [head-2],
    // i.e. the PC of the instruction that just executed).
    //
    // Gated behind JNEXT_G46B_P24 env var: per-step peek loop adds
    // wall-clock overhead but does NOT perturb emulator state, since
    // peek() bypasses the contended-read floating-bus latch update.
    {
        static const bool g46b_p24_enabled =
            std::getenv("JNEXT_G46B_P24") != nullptr;
        if (g46b_p24_enabled) {
            constexpr uint16_t WATCH_LO = 0x5BFE;
            constexpr uint16_t WATCH_HI = 0x5C03;  // exclusive
            static uint8_t  g46b_p24_prev[WATCH_HI - WATCH_LO] = {};
            static bool     g46b_p24_initialized = false;
            static int      g46b_p24_count = 0;
            constexpr int   G46B_P24_MAX = 200;
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                if (!g46b_p24_initialized) {
                    char init_buf[80] = "";
                    int n = 0;
                    for (uint16_t a = WATCH_LO; a < WATCH_HI; ++a) {
                        uint8_t v = mmu->peek(a);
                        g46b_p24_prev[a - WATCH_LO] = v;
                        n += std::snprintf(init_buf + n,
                                           sizeof(init_buf) - n,
                                           "%04x=%02x ", a, v);
                    }
                    Log::cpu()->info(
                        "G46B P24 INIT (peek) {} pc={:#06x} sp={:#06x}",
                        init_buf, pc, z80.sp.w);
                    g46b_p24_initialized = true;
                }
                if (g46b_p24_count < G46B_P24_MAX) {
                    for (uint16_t a = WATCH_LO; a < WATCH_HI; ++a) {
                        uint8_t cur = mmu->peek(a);
                        uint8_t prev = g46b_p24_prev[a - WATCH_LO];
                        if (cur != prev) {
                            int prev_idx = (g46b_pc_ring_head - 2
                                            + G46B_PC_RING_SIZE)
                                           % G46B_PC_RING_SIZE;
                            uint16_t writer_pc = g46b_pc_ring[prev_idx];
                            Log::cpu()->info(
                                "G46B P24 WRITE addr={:#06x} {:#04x}->{:#04x} "
                                "writer_pc={:#06x} cur_pc={:#06x} sp={:#06x}",
                                a, prev, cur, writer_pc, pc, z80.sp.w);
                            g46b_p24_prev[a - WATCH_LO] = cur;
                            ++g46b_p24_count;
                        }
                    }
                }
            }
        }
    }

    // G46(b) Probe 27 (TEMP — remove on G46(b) closure): trace every
    // NEXTREG $8C write (alt-ROM control). Goal: identify which write
    // disables alt-ROM (bit 7 = 0), because that flips slot 1 from
    // alt-ROM to legacy ROM mid-flight, breaking the supervisor's
    // RST $28 wrapper at PC=$3E93 (which expects NEXTREG $8E,$01;
    // RET to be there).
    //
    // Detect: opcode bytes ED 91 8C <val> (Z80N NEXTREG immediate).
    // Cap at 200 events to bound log spam.
    {
        static int g46b_p27_count = 0;
        constexpr int G46B_P27_MAX = 200;
        if (g46b_p27_count < G46B_P27_MAX) {
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                uint8_t b0 = mmu->peek(pc);
                uint8_t b1 = mmu->peek(static_cast<uint16_t>(pc + 1));
                uint8_t b2 = mmu->peek(static_cast<uint16_t>(pc + 2));
                if (b0 == 0xED && b1 == 0x91 && b2 == 0x8C) {
                    uint8_t v  = mmu->peek(static_cast<uint16_t>(pc + 3));
                    char ring[64] = "";
                    int rn = 0;
                    for (int i = G46B_PC_RING_SIZE - 5; i < G46B_PC_RING_SIZE; ++i) {
                        int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
                        rn += std::snprintf(ring + rn,
                                            sizeof(ring) - rn,
                                            "%04x ",
                                            g46b_pc_ring[idx]);
                    }
                    Log::cpu()->info(
                        "G46B P27 NEXTREG $8C,${:#04x} at PC={:#06x} "
                        "(prev5={}) altrom_en={} altrom_rw={}",
                        v, pc, ring,
                        (v & 0x80) ? 1 : 0, (v & 0x40) ? 1 : 0);
                    ++g46b_p27_count;
                }
            }
        }
    }

    // G46(b) Probe 26 (TEMP — remove on G46(b) closure): now fires up
    // to 5 times at PC=$3E93 to track slot 1 mapping changes across
    // visits. First hit should have correct NEXTREG bytes; later hits
    // start returning NOPs. Captures mmu + peek of $3E93-$3EA3.
    static int g46b_p26_count = 0;
    constexpr int G46B_P26_MAX = 5;
    if (g46b_p26_count < G46B_P26_MAX && pc == 0x3E93) {
        ++g46b_p26_count;
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            char eff_buf[64] = "";
            int en = 0;
            for (int s = 0; s < 8; ++s) {
                en += std::snprintf(eff_buf + en, sizeof(eff_buf) - en,
                                    "%02x ", mmu->get_effective_page(s));
            }
            char bytes_buf[80] = "";
            int bn = 0;
            for (uint16_t a = 0x3E93; a < 0x3E9D; ++a) {
                bn += std::snprintf(bytes_buf + bn,
                                    sizeof(bytes_buf) - bn,
                                    "%02x ", mmu->peek(a));
            }
            Log::cpu()->info(
                "G46B P26 hit#{} PC=$3E93: AF={:#06x} eff_mmu={} "
                "peek $3E93-$3E9C: {}",
                g46b_p26_count, z80.af.w, eff_buf, bytes_buf);
        }
    }

    // G46(b) Probe 25 (TEMP — remove on G46(b) closure): one-shot
    // snapshot at first PC=$000C (the DivMMC PUSH AF that writes the
    // AUTOMAP-sled target). Captures A, F, mmu[0..7], and the 16-byte
    // window of slot-0 ROM around PC so we can see which bank is
    // mapped + verify the bytes at $000B-$000D match the expected
    // POP HL ; PUSH AF ; JP $3364 wrapper.
    static bool g46b_p25_done = false;
    if (!g46b_p25_done && pc == 0x000C) {
        g46b_p25_done = true;
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            char mmu_buf[64] = "";
            int mn = 0;
            for (int s = 0; s < 8; ++s) {
                mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn,
                                    "%02x ", mmu->get_page(s));
            }
            char bytes_buf[64] = "";
            int bn = 0;
            for (uint16_t a = 0x0000; a < 0x0010; ++a) {
                bn += std::snprintf(bytes_buf + bn,
                                    sizeof(bytes_buf) - bn,
                                    "%02x ", mmu->peek(a));
            }
            Log::cpu()->info(
                "G46B P25 first PC=$000C: A={:#04x} F={:#04x} "
                "B={:#04x} C={:#04x} D={:#04x} E={:#04x} "
                "H={:#04x} L={:#04x} "
                "IX={:#06x} IY={:#06x} SP={:#06x} mmu={}",
                z80.af.b.h, z80.af.b.l,
                z80.bc.b.h, z80.bc.b.l,
                z80.de.b.h, z80.de.b.l,
                z80.hl.b.h, z80.hl.b.l,
                z80.ix.w, z80.iy.w, z80.sp.w, mmu_buf);
            Log::cpu()->info(
                "G46B P25 slot0 bytes $0000-$000F: {}", bytes_buf);
        }
    }

    // G46(b) Probe 28 (TEMP, 2026-05-08): verify RAM pre-load is effective
    // at runtime. Captures mem[$5BFE..$5C03] every time PC hits $5B20 (the
    // bank-flip wrapper RET) for the first 5 hits. Expected with sysvars.raw
    // pre-loaded: mem[$5BFF,$5C00] = $00,$FF (popped → PC=$FF00).
    static int g46b_p28_count = 0;
    if (g46b_p28_count < 5 && pc == 0x5B20) {
        ++g46b_p28_count;
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            char buf[64] = "";
            int n = 0;
            for (uint16_t a = 0x5BFC; a <= 0x5C07; ++a) {
                n += std::snprintf(buf + n, sizeof(buf) - n,
                                   "%02x ", mmu->peek(a));
            }
            uint16_t popped = static_cast<uint16_t>(mmu->peek(z80.sp.w) |
                              (mmu->peek(z80.sp.w + 1) << 8));
            Log::cpu()->info(
                "G46B P28 hit#{} PC=$5B20 RET: SP={:#06x} popped={:#06x} "
                "mem[$5BFC..$5C07]={}",
                g46b_p28_count, z80.sp.w, popped, buf);
        }
    }

    // G46(b) Probe 19 (TEMP): stack snapshot at first PC=$3D00 (the
    // AUTOMAP-sled sentinel RET). Goal: identify what's on the stack
    // that makes RET pop $0082 (vs CSpect's $0448).
    static bool g46b_p19_done = false;
    if (!g46b_p19_done && pc == 0x3D00) {
        g46b_p19_done = true;
        char stack_buf[256] = "";
        int sn = 0;
        for (int i = 0; i < 16 && sn < 240; ++i) {
            uint16_t word = static_cast<uint16_t>(
                mem_.read(z80.sp.w + i*2) |
                (mem_.read(z80.sp.w + i*2 + 1) << 8));
            sn += std::snprintf(stack_buf + sn, sizeof(stack_buf) - sn,
                                "%04x ", word);
        }
        auto rd16 = [&](uint16_t a) -> uint16_t {
            return static_cast<uint16_t>(
                mem_.read(a) | (mem_.read(a + 1) << 8));
        };
        Log::cpu()->info(
            "G46B P19 PC=$3D00 first hit: SP={:#06x} stack=[{}]",
            z80.sp.w, stack_buf);
        Log::cpu()->info(
            "G46B P19 sysvars: ($5B5A)={:#06x} ($5B6A)={:#06x} "
            "($5B8A)={:#06x} ($5B8E)={:#06x} ($5B5C)={:#04x} "
            "($5B67)={:#04x}",
            rd16(0x5B5A), rd16(0x5B6A), rd16(0x5B8A), rd16(0x5B8E),
            mem_.read(0x5B5C), mem_.read(0x5B67));
        char ring_buf[256];
        int rn = 0;
        for (int i = 0; i < G46B_PC_RING_SIZE && rn < 240; ++i) {
            int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
            rn += std::snprintf(ring_buf + rn,
                                sizeof(ring_buf) - rn,
                                "%04x ", g46b_pc_ring[idx]);
        }
        Log::cpu()->info("G46B P19 ring (oldest→newest): {}", ring_buf);
    }

    // G46(b) Probe 17 (TEMP): log each visit to a LD IX,$F700 site OR a
    // PC immediately preceding one (the gating instruction). Goal:
    // confirm whether any of the 5 sites is REACHED but skipped via
    // condition, vs never visited at all.
    static int g46b_p17_count = 0;
    if (g46b_p17_count < 60) {
        bool interesting = false;
        const char* tag = nullptr;
        switch (pc) {
            case 0x3290: tag = "RET-NC-gate-329A"; interesting = true; break;
            case 0x329A: tag = "LD-IX-F700@329A"; interesting = true; break;
            case 0x2CB0: tag = "BIT5-IY30-gate-2CB5"; interesting = true; break;
            case 0x2CB5: tag = "LD-IX-F700@2CB5"; interesting = true; break;
            case 0x3409: tag = "LD-IX-F700@3409"; interesting = true; break;
            case 0x359C: tag = "LD-IX-F700@359C"; interesting = true; break;
            case 0x2333: tag = "RET-NC-gate-2338"; interesting = true; break;
            case 0x2338: tag = "LD-IX-F700@2338"; interesting = true; break;
            default: break;
        }
        if (interesting) {
            char mmu_buf[80] = "";
            uint8_t cf = z80.af.b.l & 0x01;
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                int mn = 0;
                for (int s = 0; s < 8 && mn < 70; ++s) {
                    mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn,
                                        "%02x ", mmu->get_page(s));
                }
            }
            uint8_t b0 = mem_.read(pc);
            uint8_t b1 = mem_.read(pc + 1);
            Log::cpu()->info(
                "G46B P17 PC={:#06x} ({}) cf={} bytes={:#04x} {:#04x} ix={:#06x} mmu={}",
                pc, tag, cf, b0, b1, z80.ix.w, mmu_buf);
            ++g46b_p17_count;
        }
    }

    // G46(b) Probe 18 (TEMP — remove on G46(b) closure): ROM-bank-switch
    // tracer. Logs every CHANGE in current_rom_bank() (bit 4 of port_7FFD
    // composed with bit 2 of port_1FFD per VHDL :3691-3699). Caps at 60
    // entries to avoid log spam.
    static uint8_t g46b_rom_bank_prev = 0xFF;
    static int g46b_p18_count = 0;
    if (g46b_p18_count < 60) {
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            uint8_t bank = mmu->current_rom_bank();
            if (bank != g46b_rom_bank_prev) {
                char ring_buf[256];
                int rn = 0;
                for (int i = 0; i < G46B_PC_RING_SIZE; ++i) {
                    int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
                    rn += std::snprintf(ring_buf + rn,
                                        sizeof(ring_buf) - rn,
                                        "%04x ", g46b_pc_ring[idx]);
                }
                Log::cpu()->info(
                    "G46B P18 ROM-BANK={} (was {}) at PC={:#06x} ring={}",
                    bank, g46b_rom_bank_prev, pc, ring_buf);
                g46b_rom_bank_prev = bank;
                ++g46b_p18_count;
            }
        }
    }

    // G46(b) Probe 8 (TEMP — remove on G46(b) closure): on first hit of
    // PC=$3CE8, dump slot 0 ($0000-$1FFF) and slot 1 ($2000-$3FFF) to
    // /tmp/g46b-slot{0,1}-at-3ce8.bin so we can disassemble the runtime
    // bytes offline. Also dump the PC ring at that moment so we can see
    // the upstream caller chain that led INTO $3CE8.
    if (pc == 0x3CE8) {
        static int p8_dump_cnt = 0;
        if (p8_dump_cnt++ == 0) {
            FILE* f0 = std::fopen("/tmp/g46b-slot0-at-3ce8.bin", "wb");
            if (f0) {
                for (uint32_t a = 0x0000; a < 0x2000; ++a) {
                    uint8_t b = mem_.read(static_cast<uint16_t>(a));
                    std::fputc(b, f0);
                }
                std::fclose(f0);
            }
            FILE* f1 = std::fopen("/tmp/g46b-slot1-at-3ce8.bin", "wb");
            if (f1) {
                for (uint32_t a = 0x2000; a < 0x4000; ++a) {
                    uint8_t b = mem_.read(static_cast<uint16_t>(a));
                    std::fputc(b, f1);
                }
                std::fclose(f1);
            }
            // Also dump physical SRAM page 1 (the page the supervisor SHOULD
            // be reading from given NR_51=0xFF + ROM-in-SRAM + NR_04=0).
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                FILE* fp1 = std::fopen("/tmp/g46b-physpage01-at-3ce8.bin", "wb");
                if (fp1) {
                    const uint8_t* p1 = mmu->ram().page_ptr(0x01);
                    if (p1) std::fwrite(p1, 1, 0x2000, fp1);
                    std::fclose(fp1);
                }
                // Also page 0 for sanity
                FILE* fp0 = std::fopen("/tmp/g46b-physpage00-at-3ce8.bin", "wb");
                if (fp0) {
                    const uint8_t* p0 = mmu->ram().page_ptr(0x00);
                    if (p0) std::fwrite(p0, 1, 0x2000, fp0);
                    std::fclose(fp0);
                }
            }
            char ring_buf[256];
            int rn = 0;
            for (int i = 0; i < G46B_PC_RING_SIZE; ++i) {
                int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
                rn += std::snprintf(ring_buf + rn,
                                    sizeof(ring_buf) - rn,
                                    "%04x ", g46b_pc_ring[idx]);
            }
            char mmu_buf[80];
            int mn = 0;
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                for (int s = 0; s < 8 && mn < 70; ++s) {
                    mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn,
                                        "%02x ", mmu->get_page(s));
                }
            } else {
                std::snprintf(mmu_buf, sizeof(mmu_buf), "n/a");
            }
            // Probe 8b: capture DivMmc state at the trigger.
            const char* dm_state = "n/a";
            char dm_buf[160];
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                if (DivMmc* dm = mmu->divmmc_for_diag()) {
                    std::snprintf(dm_buf, sizeof(dm_buf),
                                  "active=%d rom_mapped=%d conmem=%d automap=%d mapram=%d bank=%d",
                                  (int)dm->is_active(), (int)dm->is_rom_mapped(),
                                  (int)dm->conmem(), (int)dm->automap_active(),
                                  (int)dm->mapram(), (int)dm->bank());
                    dm_state = dm_buf;
                    // Probe 9: dump non-zero count for each of 16 DivMMC RAM
                    // banks. Reveals whether any bank has been populated.
                    char banks_buf[256];
                    int bn = 0;
                    for (int bk = 0; bk < 16; ++bk) {
                        const uint8_t* p = dm->ram_page(bk);
                        int nz = 0;
                        if (p) for (int i = 0; i < 0x2000; ++i) if (p[i]) ++nz;
                        bn += std::snprintf(banks_buf + bn,
                                            sizeof(banks_buf) - bn,
                                            "%d:%d ", bk, nz);
                    }
                    Log::cpu()->info("  divmmc-ram-nonzero-per-bank: {}", banks_buf);
                    // Also dump each bank to a file for offline inspection
                    for (int bk = 0; bk < 16; ++bk) {
                        const uint8_t* p = dm->ram_page(bk);
                        if (!p) continue;
                        char path[64];
                        std::snprintf(path, sizeof(path),
                                      "/tmp/g46b-divmmc-bank%02d.bin", bk);
                        FILE* fp = std::fopen(path, "wb");
                        if (fp) { std::fwrite(p, 1, 0x2000, fp); std::fclose(fp); }
                    }
                }
            }
            Log::cpu()->info(
                "G46B P8: dumped slot0+slot1 at PC=$3CE8 first hit. mmu[0..7]={} divmmc=[{}] ring={}",
                mmu_buf, dm_state, ring_buf);
        }
    }

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
                    // G46(b) Probe 5: also log live NR 0x50..0x57 mapping at this hit
                    char nr5x[80];
                    int an = 0;
                    for (int i = 0; i < 8; ++i) an += std::snprintf(nr5x + an, sizeof(nr5x) - an, "%02x ", mmu->get_page(i));
                    Log::cpu()->info("PAGES PC={:#06x} mmu[0..7]={} p2f[1B:23]={} p0f[1B:23]={}", pc, nr5x, b2f, b0f);
                    ++cnt;
                }
            }
        }
    }
    // G46(b) RAM-test verification: log the value of $5B69 (last RAM-test bank)
    // at the start of supervisor self-init at $00EF and again at $20E6. If the
    // RAM-test PASS 2 completed all 0x70 banks, ($5B69) ≈ 0x6F. If it exited
    // early (bypass-mode pages 0x21..0x27 read 0 instead of $BB), ($5B69) ≈ 0.
    if (pc == 0x01CE || pc == 0x20E6 || pc == 0x2341) {
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            const uint8_t v = mmu->read(0x5B69);
            static int cnt = 0;
            if (cnt < 20) {
                Log::cpu()->info("RAMTEST PC={:#06x} ($5B69)={:#04x}  (last RAM-test bank stored)", pc, v);
                ++cnt;
            }
        }
    }
    // G46(b) Probe 3 (TEMP — remove on G46(b) closure): log SP + ($5B6A) at the
    // wrapper entry $2738 ("ld hl,($5b6a)") and exit $27A3. Captures the saved
    // opposite-stack SP that the supervisor uses to context-switch. Compare
    // bypass vs non-bypass vs CSpect.
    if (pc == 0x2738 || pc == 0x27A3) {
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            uint16_t v5b6a = static_cast<uint16_t>(
                mmu->read(0x5B6A) | (mmu->read(0x5B6B) << 8));
            uint8_t v5b8e = mmu->read(0x5B8E);
            uint16_t v5b8a = static_cast<uint16_t>(
                mmu->read(0x5B8A) | (mmu->read(0x5B8B) << 8));
            static int cnt = 0;
            if (cnt < 16) {
                Log::cpu()->info("WRAPPER PC={:#06x} sp={:#06x} ($5B6A)={:#06x} ($5B8A)={:#06x} ($5B8E)={:#04x}",
                                 pc, z80.sp.w, v5b6a, v5b8a, v5b8e);
                ++cnt;
            }
        }
    }
    // G46(b) Probe 5 (TEMP — remove on G46(b) closure): log NR_56 + NR_57 live
    // value at every wrapper IN A,($253B) read site ($27e8 reads NR_56,
    // $27f2 reads NR_57). Captures what `Mmu::get_page(i)` returns at the
    // exact moment the wrapper saves the OLD bank pair to $5B8A. Detects
    // whether the live MMU register diverges from what the supervisor expects
    // (= what CSpect would return).
    if (pc == 0x27E8 || pc == 0x27F2) {
        if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
            static int cnt = 0;
            if (cnt < 32) {
                Log::cpu()->info("WRAPPER_IN PC={:#06x} mmu[6]={:#04x} mmu[7]={:#04x}",
                                 pc, mmu->get_page(6), mmu->get_page(7));
                ++cnt;
            }
        }
    }
    // G46(b) Probe 6 (TEMP — remove on G46(b) closure): identify which
    // supervisor PC writes NR_57 = 0x0F. We track the currently-selected NR
    // by sniffing OUT $243B,N writes (any byte form: OUT (C),A/B/C/D/E/H/L
    // with BC=$243B, plus the immediate "ld a,N; out ($243b),a" form).
    // Then at any OUT to $253B with the data byte = 0x0F, log if the
    // selected reg is 0x57.
    {
        static uint8_t g46b_selected_nr = 0xFF;  // last selected NR
        uint8_t op0 = opcode;
        uint8_t op1 = mem_.read(pc + 1);
        // Sniff selects: OUT (C),r with BC=$243B
        if (op0 == 0xED && z80.bc.w == 0x243B) {
            switch (op1) {
                case 0x79: g46b_selected_nr = z80.af.b.h; break;  // OUT (C),A
                case 0x41: g46b_selected_nr = z80.bc.b.h; break;  // OUT (C),B
                case 0x49: g46b_selected_nr = z80.bc.b.l; break;  // OUT (C),C
                case 0x51: g46b_selected_nr = z80.de.b.h; break;  // OUT (C),D
                case 0x59: g46b_selected_nr = z80.de.b.l; break;  // OUT (C),E
                case 0x61: g46b_selected_nr = z80.hl.b.h; break;  // OUT (C),H
                case 0x69: g46b_selected_nr = z80.hl.b.l; break;  // OUT (C),L
                default: break;
            }
        }
        // Sniff "OUT ($243B),A" (D3 3B) — port-immediate form
        if (op0 == 0xD3 && op1 == 0x3B) {
            g46b_selected_nr = z80.af.b.h;
        }
        // Detect NR_57 = 0x0F via NEXTREG immediate (ED 91 57 0F)
        if (op0 == 0xED && op1 == 0x91) {
            uint8_t op2 = mem_.read(pc + 2);
            uint8_t op3 = mem_.read(pc + 3);
            if (op2 == 0x57 && op3 == 0x0F) {
                static int cnt = 0;
                if (cnt < 32) {
                    Log::cpu()->info(
                        "G46B NEXTREG_57=0x0F via NEXTREG-imm PC={:#06x}", pc);
                    ++cnt;
                }
            }
        }
        // Detect NR_57 = 0x0F via NEXTREG-REG (ED 92 57) — value comes from A
        if (op0 == 0xED && op1 == 0x92) {
            uint8_t op2 = mem_.read(pc + 2);
            if (op2 == 0x57 && z80.af.b.h == 0x0F) {
                static int cnt = 0;
                if (cnt < 32) {
                    char mmu_buf[80];
                    int mn = 0;
                    if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                        for (int s = 0; s < 8 && mn < 70; ++s) {
                            mn += std::snprintf(mmu_buf + mn, sizeof(mmu_buf) - mn,
                                                "%02x ", mmu->get_page(s));
                        }
                    } else {
                        std::snprintf(mmu_buf, sizeof(mmu_buf), "n/a");
                    }
                    // Dump bytes at PC..PC+7 so we can confirm slot mapping
                    char bytes_buf[40];
                    int bn = 0;
                    for (int i = 0; i < 8 && bn < 35; ++i) {
                        bn += std::snprintf(bytes_buf + bn, sizeof(bytes_buf) - bn,
                                            "%02x ", mem_.read(pc + i));
                    }
                    uint16_t sp = z80.sp.w;
                    uint16_t tos0 = static_cast<uint16_t>(
                        mem_.read(sp) | (mem_.read(sp + 1) << 8));
                    uint16_t tos1 = static_cast<uint16_t>(
                        mem_.read(sp + 2) | (mem_.read(sp + 3) << 8));
                    Log::cpu()->info(
                        "G46B NEXTREG_57=0x0F via NEXTREG-A PC={:#06x} sp={:#06x} tos0={:#06x} tos1={:#06x} mmu[0..7]={}",
                        pc, sp, tos0, tos1, mmu_buf);
                    // Probe 7: at $008E (the runtime trampoline) only, dump
                    // the ring buffer of last N PCs so we can see the caller
                    // path leading into the trampoline.
                    if (pc == 0x008E) {
                        char ring_buf[256];
                        int rn = 0;
                        for (int i = 0; i < G46B_PC_RING_SIZE; ++i) {
                            int idx = (g46b_pc_ring_head + i) % G46B_PC_RING_SIZE;
                            rn += std::snprintf(ring_buf + rn,
                                                sizeof(ring_buf) - rn,
                                                "%04x ", g46b_pc_ring[idx]);
                        }
                        Log::cpu()->info("  ring (oldest→newest): {}", ring_buf);
                        // Dump runtime slot 0 boot/dispatch area at trigger
                        // time. Dedicated counter for $008E (the outer cnt
                        // is consumed by $013D / $019A hits which fire first).
                        static int slot0_dump_cnt = 0;
                        if (slot0_dump_cnt++ < 1) {
                            for (uint16_t row = 0x0000; row < 0x0100; row += 16) {
                                char rb[80];
                                int rbn = 0;
                                for (int i = 0; i < 16 && rbn < 70; ++i) {
                                    rbn += std::snprintf(rb + rbn,
                                                         sizeof(rb) - rbn,
                                                         "%02x ",
                                                         mem_.read(row + i));
                                }
                                Log::cpu()->info("  slot0 ${:04x}: {}", row, rb);
                            }
                        }
                    }
                    ++cnt;
                }
            }
        }
        // Detect NR_57 = 0x0F via OUT (C),r to $253B
        if (op0 == 0xED && z80.bc.w == 0x253B && g46b_selected_nr == 0x57) {
            uint8_t v = 0xFF;
            const char* src = nullptr;
            switch (op1) {
                case 0x79: v = z80.af.b.h; src = "A"; break;
                case 0x41: v = z80.bc.b.h; src = "B"; break;
                case 0x49: v = z80.bc.b.l; src = "C"; break;
                case 0x51: v = z80.de.b.h; src = "D"; break;
                case 0x59: v = z80.de.b.l; src = "E"; break;
                case 0x61: v = z80.hl.b.h; src = "H"; break;
                case 0x69: v = z80.hl.b.l; src = "L"; break;
                default: break;
            }
            if (src && v == 0x0F) {
                static int cnt = 0;
                if (cnt < 32) {
                    uint16_t caller = static_cast<uint16_t>(
                        mem_.read(z80.sp.w) | (mem_.read(z80.sp.w + 1) << 8));
                    Log::cpu()->info(
                        "G46B NEXTREG_57=0x0F via OUT (C),{} PC={:#06x} retaddr={:#06x}",
                        src, pc, caller);
                    ++cnt;
                }
            }
        }
        // Also detect via OUT ($253B),A
        if (op0 == 0xD3 && op1 == 0x3B && z80.af.b.h == 0x0F && g46b_selected_nr == 0x57) {
            static int cnt = 0;
            if (cnt < 32) {
                Log::cpu()->info(
                    "G46B NEXTREG_57=0x0F via OUT ($253B),A PC={:#06x}", pc);
                ++cnt;
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
    // G46(b) v6 ISOLATION TEST: when JNEXT_G46B_FORCE_IX is set, force
    // IX=\$F700 at the first ~16 hits of PC=\$1800 (print routine entry).
    // CSpect reaches \$20E6 with IX=\$F700 (sprite descriptor table base).
    // Our jnext reaches with IX=\$E01B. If forcing IX=\$F700 here makes
    // the welcome screen render, IX is the only divergence. If still
    // stalls, there are downstream divergences too.
    if (pc == 0x1800) {
        static const bool force_ix = std::getenv("JNEXT_G46B_FORCE_IX") != nullptr;
        static int force_count = 0;
        if (force_ix && force_count < 16) {
            ++force_count;
            // Also force NR_57 = $10 (= map slot 7 to page 0x30 where the
            // pre-loaded CSpect sprite descriptor lives at offset $1700).
            if (auto* mmu = dynamic_cast<Mmu*>(&mem_)) {
                mmu->set_page(7, 0x10);
            }
            Log::cpu()->info("G46B FORCE_IX hit #{} at PC=0x1800: IX was 0x{:04x} -> 0xF700, NR_57 -> 0x10",
                             force_count, z80.ix.w);
            z80.ix.w = 0xF700;
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
