/* fuse_z80_core.c — Standalone Z80 core derived from FUSE 1.6.0.
 *
 * Original FUSE Z80 core: Copyright (c) 1999-2016 Philip Kendall, Stuart Brady
 * Adaptation for jnext:   Copyright (c) 2026 Jorge Giner Cordero
 *
 * Licensed under the GNU General Public License version 2 or later.
 * See COPYING.GPL2 in this directory.
 *
 * This file combines the flag table initialisation (from z80.c), the reset
 * logic, interrupt handling, and a stripped-down single-instruction execution
 * loop (from z80_ops.c).  All FUSE-specific features (RZX, debugger,
 * peripheral paging, profiler, event system) have been removed.
 *
 * The tape_save_trap() and tape_load_trap() calls in opcodes_base.c are
 * stubbed to return -1 (non-zero = don't intercept) in fuse_z80_shim.h.
 */

/* Include order matters: shim provides types and processor struct,
 * z80_macros.h provides register aliases and instruction macros.
 * Then we override FUSE's contention/IS_CMOS/memory macros with our own. */
#include "fuse_z80_shim.h"
#include "z80_macros.h"

/* Override FUSE contention macros — our emulator handles contention at a
 * higher level; the Z80 core just advances tstates by the base time. */
#undef contend_read
#undef contend_read_no_mreq
#undef contend_write_no_mreq
#define contend_read(address, time)          tstates += (time);
#define contend_read_no_mreq(address, time)  tstates += (time);
#define contend_write_no_mreq(address, time) tstates += (time);

/* Override IS_CMOS — we emulate NMOS Z80 */
#undef IS_CMOS
#define IS_CMOS 0

/* Map FUSE memory/IO function names to our callbacks */
#undef readbyte
#undef readbyte_internal
#undef writebyte
#undef readport
#undef writeport
#define readbyte(addr)          fuse_z80_readbyte(addr)
#define readbyte_internal(addr) fuse_z80_readbyte(addr)
#define writebyte(addr, val)    fuse_z80_writebyte(addr, val)
#define readport(port)          fuse_z80_readport(port)
#define writeport(port, val)    fuse_z80_writeport(port, val)

/* ── Global state ──────────────────────────────────────────────────────── */

processor z80;
libspectrum_dword tstates = 0;
libspectrum_dword event_next_event = 0;

/* RZX / event stubs */
int rzx_instructions_offset = 0;
int z80_interrupt_event = 0;
int z80_nmos_iff2_event = 0;

/* ── Flag tables ───────────────────────────────────────────────────────── */

const libspectrum_byte halfcarry_add_table[] =
    { 0, 0x10, 0x10, 0x10, 0, 0, 0, 0x10 };
const libspectrum_byte halfcarry_sub_table[] =
    { 0, 0, 0x10, 0, 0x10, 0, 0x10, 0x10 };
const libspectrum_byte overflow_add_table[] = { 0, 0, 0, 0x04, 0x04, 0, 0, 0 };
const libspectrum_byte overflow_sub_table[] = { 0, 0x04, 0, 0, 0, 0, 0x04, 0 };

libspectrum_byte sz53_table[0x100];
libspectrum_byte parity_table[0x100];
libspectrum_byte sz53p_table[0x100];

void fuse_z80_init_tables(void)
{
    int i, j, k;
    libspectrum_byte parity;

    for (i = 0; i < 0x100; i++) {
        sz53_table[i] = (libspectrum_byte)(i & (FLAG_3 | FLAG_5 | FLAG_S));
        j = i;
        parity = 0;
        for (k = 0; k < 8; k++) { parity ^= j & 1; j >>= 1; }
        parity_table[i] = (libspectrum_byte)(parity ? 0 : FLAG_P);
        sz53p_table[i] = sz53_table[i] | parity_table[i];
    }

    sz53_table[0]  |= FLAG_Z;
    sz53p_table[0] |= FLAG_Z;
}

/* ── Reset ─────────────────────────────────────────────────────────────── */

void fuse_z80_reset(int hard_reset)
{
    AF = AF_ = 0xffff;
    I = R = R7 = 0;
    PC = 0;
    SP = 0xffff;
    IFF1 = IFF2 = IM = 0;
    z80.halted = 0;
    z80.iff2_read = 0;
    Q = 0;

    if (hard_reset) {
        BC = DE = HL = 0;
        BC_ = DE_ = HL_ = 0;
        IX = IY = 0;
        z80.memptr.w = 0;
    }

    z80.interrupts_enabled_at = -1;
}

/* ── Interrupt handling ────────────────────────────────────────────────── */

int fuse_z80_interrupt(libspectrum_byte vector)
{
    if (!IFF1) return 0;

    /* If interrupts were just enabled, don't accept yet */
    if ((libspectrum_signed_dword)tstates == z80.interrupts_enabled_at)
        return 0;

    if (z80.iff2_read && !IS_CMOS) {
        F &= ~FLAG_P;
    }

    if (z80.halted) { PC++; z80.halted = 0; }

    IFF1 = IFF2 = 0;
    R++;

    tstates += 7; /* extended M1 cycle */

    writebyte(--SP, PCH);
    writebyte(--SP, PCL);

    switch (IM) {
        case 0:
            PC = 0x0038;
            break;
        case 1:
            PC = 0x0038;
            break;
        case 2:
        {
            libspectrum_word inttemp = (0x100 * I) + vector;
            PCL = readbyte(inttemp++);
            PCH = readbyte(inttemp);
            break;
        }
        default:
            PC = 0x0038;
            break;
    }

    z80.memptr.w = PC;
    Q = 0;

    return 1; /* accepted */
}

void fuse_z80_nmi(void)
{
    if (z80.halted) { PC++; z80.halted = 0; }

    IFF1 = 0;
    R++;
    tstates += 5;

    writebyte(--SP, PCH);
    writebyte(--SP, PCL);

    Q = 0;
    PC = 0x0066;
}

/* ── Single-instruction execution ──────────────────────────────────────── */

/* With HAVE_ENOUGH_MEMORY, opcodes_base.c inlines all prefix handlers
 * as nested switch statements rather than calling separate functions.
 * The DD/FD default case uses "goto end_opcode" to re-dispatch an
 * instruction that doesn't involve IX/IY through the main switch. */
#define HAVE_ENOUGH_MEMORY 1

int fuse_z80_execute_one(void)
{
    libspectrum_byte opcode;
    libspectrum_byte last_Q;
    libspectrum_dword start_tstates = tstates;

    z80.iff2_read = 0;

    /* Opcode fetch contention + read */
    contend_read(PC, 4);
    opcode = readbyte_internal(PC);

end_opcode:
    /* This label is the target of "goto end_opcode" in z80_ddfd.c's default
     * case, which backs up PC and R and sets opcode to the re-dispatched byte.
     * We then fall through to the normal PC++, R++, and switch. */
    PC++;
    R++;
    last_Q = Q;
    Q = 0;

    switch (opcode) {
#include "opcodes_base.c"
    }

    return (int)(tstates - start_tstates);
}
