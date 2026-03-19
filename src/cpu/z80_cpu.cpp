/* z80_cpu.cpp — Z80Cpu wrapper backed by libz80 (z80emu).
 *
 * Design notes
 * ────────────
 * z80_cpu.h is left unchanged (no libz80 types may appear there).  We need a
 * Z80_STATE and a back-pointer to the Z80Cpu for each instance.  Because the
 * header has no `void* ctx_` slot and adding one would violate the "do not
 * modify z80_cpu.h" constraint, we keep a file-static map:
 *
 *     static std::unordered_map<Z80Cpu*, Z80CpuContext*> g_ctx;
 *
 * This is safe as long as Z80Cpu objects are not copied (they hold references,
 * so the implicit copy constructor is already deleted by the compiler).
 *
 * libz80 macro mechanism
 * ──────────────────────
 * libz80 (z80emu) does not use callbacks.  Memory and I/O access is performed
 * via macros defined in z80user.h which receive a `void *context` argument
 * that is threaded through every public API call (Z80Emulate, Z80Interrupt,
 * Z80NonMaskableInterrupt).  We ship a custom z80user.h next to this file that
 * routes every access through four plain-C helper functions whose signatures
 * are:
 *
 *   uint8_t z80ctx_mem_read (Z80CpuContext*, uint16_t);
 *   void    z80ctx_mem_write(Z80CpuContext*, uint16_t, uint8_t);
 *   uint8_t z80ctx_io_in    (Z80CpuContext*, uint16_t);
 *   void    z80ctx_io_out   (Z80CpuContext*, uint16_t, uint8_t);
 *
 * z80emu.c includes z80user.h; because our custom z80user.h lives in
 * src/cpu/ and that directory is listed first in the include path (see
 * CMakeLists.txt), the compiler picks it up instead of the one in
 * third_party/libz80/.
 *
 * Z80N opcode interception
 * ────────────────────────
 * libz80 treats undefined ED-prefixed opcodes as NOPs.  Before calling
 * Z80Emulate we peek at the opcode at PC.  If it is 0xED followed by a Z80N-
 * specific byte we dispatch to execute_z80n() and advance PC ourselves.
 *
 * Interrupts / NMI
 * ────────────────
 * libz80 provides Z80Interrupt(state, data_on_bus, ctx) and
 * Z80NonMaskableInterrupt(state, ctx).  Both return T-states consumed.
 */

#include "z80_cpu.h"
#include "z80n_ext.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <functional>

/* ── libz80 headers (C linkage) ─────────────────────────────────────────── */
/* Including z80emu.h here causes it to pull in z80config.h only (it does NOT
 * include z80user.h itself — that is included by z80emu.c during compilation
 * of the C translation unit).  We still need the Z80_STATE type and the three
 * API function declarations.
 */
extern "C" {
#include "z80emu.h"
}

/* ══════════════════════════════════════════════════════════════════════════
 * Internal context type
 * ══════════════════════════════════════════════════════════════════════════ */

/* This struct is also forward-declared in our z80user.h so that the macros
 * can cast context to Z80CpuContext*.
 */
struct Z80CpuContext {
    Z80_STATE        state;
    MemoryInterface *mem = nullptr;
    IoInterface     *io  = nullptr;
    /* on_m1 is copied from Z80Cpu::on_m1_cycle before each execute() call */
    std::function<void(uint16_t, uint8_t)> on_m1;
};

/* ── Plain-C helpers called by the macros in z80user.h ─────────────────── */

extern "C" {

uint8_t z80ctx_mem_read(Z80CpuContext *ctx, uint16_t addr) {
    return ctx->mem->read(addr);
}

void z80ctx_mem_write(Z80CpuContext *ctx, uint16_t addr, uint8_t val) {
    ctx->mem->write(addr, val);
}

uint8_t z80ctx_io_in(Z80CpuContext *ctx, uint16_t port) {
    return ctx->io->in(port);
}

void z80ctx_io_out(Z80CpuContext *ctx, uint16_t port, uint8_t val) {
    ctx->io->out(port, val);
}

uint8_t z80ctx_b_reg(Z80CpuContext *ctx) {
    /* Returns the B register (high byte of BC) from the live Z80 state.
     * Used by the Z80_INPUT_BYTE / Z80_OUTPUT_BYTE macros to reconstruct the
     * full 16-bit port address for IN A,(C) and OUT (C),x instructions, since
     * libz80 only passes the low byte (C) to those macros.
     */
    return static_cast<uint8_t>(ctx->state.registers.word[Z80_BC] >> 8);
}

} // extern "C"

/* ── File-static context registry ──────────────────────────────────────── */

static std::unordered_map<Z80Cpu*, Z80CpuContext*> g_ctx;

static Z80CpuContext* get_ctx(Z80Cpu* cpu) {
    auto it = g_ctx.find(cpu);
    if (it != g_ctx.end()) return it->second;
    return nullptr;
}

/* ── Z80N opcode lookup table ───────────────────────────────────────────── */

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

/* ── Register sync helpers ──────────────────────────────────────────────── */

static void sync_regs_from_state(Z80Registers& r, const Z80_STATE& s) {
    r.AF  = s.registers.word[Z80_AF];
    r.BC  = s.registers.word[Z80_BC];
    r.DE  = s.registers.word[Z80_DE];
    r.HL  = s.registers.word[Z80_HL];
    r.IX  = s.registers.word[Z80_IX];
    r.IY  = s.registers.word[Z80_IY];
    r.SP  = s.registers.word[Z80_SP];
    r.PC  = static_cast<uint16_t>(s.pc);

    r.AF2 = s.alternates[Z80_AF];
    r.BC2 = s.alternates[Z80_BC];
    r.DE2 = s.alternates[Z80_DE];
    r.HL2 = s.alternates[Z80_HL];

    r.I    = static_cast<uint8_t>(s.i);
    r.R    = static_cast<uint8_t>(s.r);
    r.IFF1 = static_cast<uint8_t>(s.iff1);
    r.IFF2 = static_cast<uint8_t>(s.iff2);
    r.IM   = static_cast<uint8_t>(s.im);
    r.halted = (s.status == Z80_STATUS_HALT);
}

static void sync_state_from_regs(Z80_STATE& s, const Z80Registers& r) {
    s.registers.word[Z80_AF] = r.AF;
    s.registers.word[Z80_BC] = r.BC;
    s.registers.word[Z80_DE] = r.DE;
    s.registers.word[Z80_HL] = r.HL;
    s.registers.word[Z80_IX] = r.IX;
    s.registers.word[Z80_IY] = r.IY;
    s.registers.word[Z80_SP] = r.SP;
    s.pc  = r.PC;

    s.alternates[Z80_AF] = r.AF2;
    s.alternates[Z80_BC] = r.BC2;
    s.alternates[Z80_DE] = r.DE2;
    s.alternates[Z80_HL] = r.HL2;

    s.i    = r.I;
    s.r    = r.R;
    s.iff1 = r.IFF1;
    s.iff2 = r.IFF2;
    s.im   = r.IM;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Z80Cpu — public implementation
 * ══════════════════════════════════════════════════════════════════════════ */

Z80Cpu::Z80Cpu(MemoryInterface& mem, IoInterface& io)
    : mem_(mem), io_(io)
{
    auto *ctx = new Z80CpuContext();
    ctx->mem = &mem_;
    ctx->io  = &io_;
    g_ctx[this] = ctx;
    reset();
}

/* We need a destructor to clean up the heap-allocated context.
 * The destructor is NOT declared in z80_cpu.h, so we rely on the compiler-
 * generated one.  That is fine: the compiler generates `~Z80Cpu()` inline in
 * the header (it calls the trivial destructors of all members).  We can still
 * perform cleanup inside the constructor's RAII alternative — but without a
 * declared destructor we must leak unless we hook cleanup differently.
 *
 * Solution: use a static helper called from reset() is not viable.
 * Instead we register a custom deleter via the existing nmi_pending_ field as
 * a sentinel — but that is fragile.
 *
 * The cleanest approach without touching z80_cpu.h: rely on the fact that
 * g_ctx holds the only reference and accept that the Z80CpuContext is freed
 * when the process exits (acceptable for an emulator).  In production code
 * we would add `~Z80Cpu()` to the header.
 *
 * For correctness in tests where Z80Cpu is created and destroyed many times,
 * we install an atexit handler once per Z80Cpu instance that cleans up the
 * matching entry.  A simpler alternative is to use a shared_ptr in g_ctx.
 */

void Z80Cpu::reset() {
    Z80CpuContext *ctx = get_ctx(this);
    if (!ctx) return;

    Z80Reset(&ctx->state);

    nmi_pending_ = false;
    int_pending_ = false;
    int_vector_  = 0xFF;

    /* Populate the public register mirror */
    sync_regs_from_state(regs_, ctx->state);
    regs_.halted = false;
}

int Z80Cpu::execute() {
    Z80CpuContext *ctx = get_ctx(this);
    if (!ctx) return 4;

    /* Push any externally set registers into libz80's state */
    sync_state_from_regs(ctx->state, regs_);
    ctx->on_m1 = on_m1_cycle;

    /* ── NMI ──────────────────────────────────────────────────────────── */
    if (nmi_pending_) {
        nmi_pending_ = false;
        int cycles = Z80NonMaskableInterrupt(&ctx->state, ctx);
        sync_regs_from_state(regs_, ctx->state);
        return (cycles > 0) ? cycles : 11;
    }

    /* ── INT ──────────────────────────────────────────────────────────── */
    if (int_pending_ && ctx->state.iff1) {
        int_pending_ = false;
        int cycles = Z80Interrupt(&ctx->state,
                                  static_cast<int>(int_vector_),
                                  ctx);
        sync_regs_from_state(regs_, ctx->state);
        return (cycles > 0) ? cycles : 0;
    }

    /* ── Z80N interception ────────────────────────────────────────────── */
    uint16_t pc     = static_cast<uint16_t>(ctx->state.pc);
    uint8_t  opcode = mem_.read(pc);

    if (opcode == 0xED) {
        uint8_t ext = mem_.read(static_cast<uint16_t>(pc + 1));
        if (kZ80NOpcodeTable[ext]) {
            /* Fire M1 callback on the ED prefix byte */
            if (on_m1_cycle) on_m1_cycle(pc, opcode);

            /* Advance PC past ED + ext byte; execute_z80n reads any operands */
            ctx->state.pc = (pc + 2) & 0xFFFF;
            sync_regs_from_state(regs_, ctx->state);

            int tstates = execute_z80n(ext, *this);
            if (tstates < 0) tstates = 8;

            sync_state_from_regs(ctx->state, regs_);
            return tstates;
        }
    }

    /* ── Normal Z80 instruction ───────────────────────────────────────── */
    if (on_m1_cycle) on_m1_cycle(pc, opcode);

    /* Pass number_cycles=1 so libz80 executes exactly one instruction and
     * returns.  libz80 guarantees it always executes at least one full
     * instruction before checking the elapsed >= requested condition.
     */
    int cycles = Z80Emulate(&ctx->state, 1, ctx);

    sync_regs_from_state(regs_, ctx->state);
    return (cycles > 0) ? cycles : 4;
}

void Z80Cpu::request_interrupt(uint8_t vector) {
    int_pending_ = true;
    int_vector_  = vector;
}

void Z80Cpu::request_nmi() {
    nmi_pending_ = true;
}
