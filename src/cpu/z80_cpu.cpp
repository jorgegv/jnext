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
#include <cstring>

// ── FUSE Z80 core (C linkage) ───────────────────────────────────────────

extern "C" {
#include "fuse_z80_shim.h"
}

// ── FUSE built-in contention data (defined here, declared in shim) ──────

memory_page_entry_t memory_map_read[8]  = {};
memory_page_entry_t memory_map_write[8] = {};
libspectrum_dword ula_contention[ULA_CONTENTION_TABLE_SIZE]        = {};
libspectrum_dword ula_contention_no_mreq[ULA_CONTENTION_TABLE_SIZE] = {};

// ── Memory/IO callback state ────────────────────────────────────────────

// The FUSE opcode files call readbyte/writebyte/readport/writeport which
// are macros that expand to fuse_z80_readbyte() etc.  These C functions
// dispatch through the active Z80Cpu instance's MemoryInterface/IoInterface.
static MemoryInterface* s_mem = nullptr;
static IoInterface*     s_io  = nullptr;

// Contention callback: called on each memory access to a potentially
// contended address. The callback adds the contention delay to tstates.
static std::function<void(uint16_t addr)>* s_contention_cb = nullptr;

// ── Phase-2 contention runtime (2026-04-26) ────────────────────────────
// Per-cycle VHDL-faithful contention via ContentionModel::contention_tick().
// Set by Emulator::init() through z80_set_contention_runtime(); when null,
// no contention is applied (FUSE Z80 test harness path — preserves the
// 1356/1356 compliance score).
//
// Per `feedback_lang_c_builds.md` the FUSE table was the prior path but
// is now retired for the production emulator. The FUSE table symbols
// (memory_map_read, memory_map_write, ula_contention*) stay as zero-filled
// definitions because the FUSE Z80 opcode files reference them via the
// `contend_read`/`contend_write` macros — but those macros expand to
// `fuse_z80_readbyte_internal` calls which never path through the FUSE
// table directly; we override the externally-linked callbacks here.
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
            // Fire M1 callback on the ED prefix byte
            if (on_m1_cycle) on_m1_cycle(pc, opcode);

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

void z80_build_contention_tables(MachineType /*type*/)
{
    // Phase-2 retirement (2026-04-26): the FUSE `ula_contention[]` /
    // `memory_map_read[]` tables are no longer the source of contention
    // delays in the production emulator. Contention now flows through
    // ContentionModel::contention_tick() per-cycle (see
    // fuse_z80_readbyte/writebyte/readport/writeport above).
    //
    // The FUSE table symbols stay defined (zero-filled at file scope
    // above) because the FUSE Z80 opcode files reference them via the
    // `contend_read` / `contend_write` macros expanded inside the FUSE
    // sources; with our overridden `fuse_z80_readbyte` / `fuse_z80_writebyte`
    // they're effectively unused but the externs must resolve at link
    // time. Leaving them zero is safe — any read returns 0 delay.
    //
    // The FUSE Z80 opcode test suite (`fuse_z80_test`) does NOT install
    // the contention runtime via z80_set_contention_runtime() — its
    // baseline 1356/1356 score relies on contention being inert on that
    // path, which the early-return at the top of each callback preserves
    // when `s_contention == nullptr`.
    std::memset(ula_contention, 0, sizeof(ula_contention));
    std::memset(ula_contention_no_mreq, 0, sizeof(ula_contention_no_mreq));
    std::memset(memory_map_read, 0, sizeof(memory_map_read));
    std::memset(memory_map_write, 0, sizeof(memory_map_write));
}

void z80_set_page_contended(int /*page*/, bool /*contended*/)
{
    // Phase-2 retirement (2026-04-26): legacy hook — no-op. Per-page
    // contention is decoded inside ContentionModel via mem_active_page
    // (set per-cycle from Mmu::get_effective_page() in the FUSE callbacks
    // above). The Emulator port-handler call sites still call this for
    // compile-time symbol stability; it has no runtime effect.
}

void z80_set_contention_runtime(ContentionModel* cm, Mmu* mmu, MachineType machine_type)
{
    s_contention     = cm;
    s_contention_mmu = mmu;
    const auto t = machine_timing(machine_type);
    s_tstates_per_line  = t.tstates_per_line;
    s_tstates_per_frame = t.tstates_per_frame;
}
