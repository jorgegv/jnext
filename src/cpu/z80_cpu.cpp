/* z80_cpu.cpp — Z80Cpu wrapper backed by FUSE Z80 core.
 *
 * Design notes
 * ────────────
 * z80_cpu.h is left unchanged (no FUSE types may appear there).  The FUSE Z80
 * core uses a global `processor z80` struct and global `tstates` counter.
 * We provide the memory/IO callback functions that the FUSE opcode files call
 * via macros defined in fuse_z80_shim.h.
 *
 * The FUSE core is inherently single-instance (global state).  This matches
 * our emulator's single-CPU architecture.
 *
 * Z80N opcode interception
 * ────────────────────────
 * FUSE's Z80 core treats undefined ED-prefixed opcodes as NOPs.  Before calling
 * fuse_z80_execute_one() we peek at the opcode at PC.  If it is 0xED followed
 * by a Z80N-specific byte we dispatch to execute_z80n() and advance PC ourselves.
 */

#include "z80_cpu.h"
#include "z80n_ext.h"
#include "core/log.h"

#include <cstdint>
#include <cstring>

/* ── FUSE Z80 core (C linkage) ─────────────────────────────────────────── */

extern "C" {
#include "fuse_z80_shim.h"
}

/* ── Memory/IO callback state ──────────────────────────────────────────── */

/* The FUSE opcode files call readbyte/writebyte/readport/writeport which
 * are macros that expand to fuse_z80_readbyte() etc.  These C functions
 * dispatch through the active Z80Cpu instance's MemoryInterface/IoInterface.
 */
static MemoryInterface* s_mem = nullptr;
static IoInterface*     s_io  = nullptr;

extern "C" {

libspectrum_byte fuse_z80_readbyte(libspectrum_word address) {
    return s_mem->read(address);
}

void fuse_z80_writebyte(libspectrum_word address, libspectrum_byte b) {
    s_mem->write(address, b);
}

libspectrum_byte fuse_z80_readport(libspectrum_word port) {
    return s_io->in(port);
}

void fuse_z80_writeport(libspectrum_word port, libspectrum_byte b) {
    s_io->out(port, b);
}

} // extern "C"

/* ── Z80N opcode lookup table ──────────────────────────────────────────── */

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
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIX)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDDX)]       = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIRX)]      = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDIRSCALE)]  = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDPIRX)]     = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LDDRX)]      = true;
    kZ80NOpcodeTable[static_cast<uint8_t>(Z80NOpcode::LOOP)]       = true;
    return true;
}
static bool z80n_table_initialized = init_z80n_table();

/* ── Register sync helpers ─────────────────────────────────────────────── */

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
    r.IFF1 = z80.iff1;
    r.IFF2 = z80.iff2;
    r.IM   = z80.im;
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
    z80.iff1  = r.IFF1;
    z80.iff2  = r.IFF2;
    z80.im    = r.IM;
    z80.halted = r.halted ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Z80Cpu — public implementation
 * ══════════════════════════════════════════════════════════════════════════ */

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

    fuse_z80_reset(1); /* hard reset */
    tstates = 0;

    nmi_pending_ = false;
    int_pending_ = false;
    int_vector_  = 0xFF;

    sync_regs_from_fuse(regs_);
}

int Z80Cpu::execute() {
    s_mem = &mem_;
    s_io  = &io_;

    /* Push any externally set registers into FUSE state */
    sync_fuse_from_regs(regs_);

    /* ── NMI ──────────────────────────────────────────────────────────── */
    if (nmi_pending_) {
        nmi_pending_ = false;
        Log::cpu()->debug("NMI at PC={:#06x}", z80.pc.w);
        libspectrum_dword before = tstates;
        fuse_z80_nmi();
        sync_regs_from_fuse(regs_);
        int cycles = (int)(tstates - before);
        return (cycles > 0) ? cycles : 11;
    }

    /* ── INT ──────────────────────────────────────────────────────────── */
    if (int_pending_ && z80.iff1) {
        Log::cpu()->debug("INT vector={:#04x} at PC={:#06x}", int_vector_, z80.pc.w);
        libspectrum_dword before = tstates;
        int accepted = fuse_z80_interrupt(int_vector_);
        sync_regs_from_fuse(regs_);
        if (accepted) {
            int_pending_ = false;
            return (int)(tstates - before);
        }
        /* If not accepted (e.g. interrupts_enabled_at == tstates), keep
         * int_pending_ true so the interrupt is retried next cycle.
         * Fall through to execute one instruction first. */
    }

    /* ── Z80N interception ────────────────────────────────────────────── */
    uint16_t pc = z80.pc.w;
    uint8_t  opcode = mem_.read(pc);

    if (opcode == 0xED) {
        uint8_t ext = mem_.read(static_cast<uint16_t>(pc + 1));
        if (kZ80NOpcodeTable[ext]) {
            Log::cpu()->trace("Z80N opcode ED {:#04x} at PC={:#06x}", ext, pc);
            /* Fire M1 callback on the ED prefix byte */
            if (on_m1_cycle) on_m1_cycle(pc, opcode);

            /* Advance PC past ED + ext byte; execute_z80n reads any operands */
            z80.pc.w = (pc + 2) & 0xFFFF;
            sync_regs_from_fuse(regs_);

            int t = execute_z80n(ext, *this);
            if (t < 0) t = 8;

            sync_fuse_from_regs(regs_);
            return t;
        }
    }

    /* ── Normal Z80 instruction ───────────────────────────────────────── */
    if (on_m1_cycle) on_m1_cycle(pc, opcode);

    int cycles = fuse_z80_execute_one();

    sync_regs_from_fuse(regs_);
    return (cycles > 0) ? cycles : 4;
}

void Z80Cpu::request_interrupt(uint8_t vector) {
    int_pending_ = true;
    int_vector_  = vector;
}

void Z80Cpu::request_nmi() {
    nmi_pending_ = true;
}
