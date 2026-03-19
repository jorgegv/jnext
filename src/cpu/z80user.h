/* z80user.h — custom libz80 user macros for the ZX Spectrum Next emulator.
 *
 * This file replaces the default z80user.h shipped with libz80.  It routes
 * all memory / IO accesses through the Z80Cpu context pointer that is passed
 * to Z80Emulate / Z80Interrupt / Z80NonMaskableInterrupt.
 *
 * The context pointer is always a Z80CpuContext* (defined in z80_cpu.cpp).
 */

#ifndef __Z80USER_INCLUDED__
#define __Z80USER_INCLUDED__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declare the opaque context type so the macros can cast cleanly.
 * The actual struct is defined in z80_cpu.cpp.
 */
struct Z80CpuContext;

/* ── Memory read (fetch) ────────────────────────────────────────────────── */

#define Z80_READ_BYTE(address, x)                                       \
{                                                                       \
    (x) = z80ctx_mem_read((struct Z80CpuContext *)(context),           \
                          (uint16_t)((address) & 0xffff));             \
}

#define Z80_FETCH_BYTE(address, x)   Z80_READ_BYTE((address), (x))

#define Z80_READ_WORD(address, x)                                       \
{                                                                       \
    uint16_t _lo, _hi;                                                  \
    _lo = z80ctx_mem_read((struct Z80CpuContext *)(context),           \
                          (uint16_t)((address) & 0xffff));             \
    _hi = z80ctx_mem_read((struct Z80CpuContext *)(context),           \
                          (uint16_t)(((address) + 1) & 0xffff));       \
    (x) = (unsigned)_lo | ((unsigned)_hi << 8);                        \
}

#define Z80_FETCH_WORD(address, x)   Z80_READ_WORD((address), (x))

/* ── Memory write ───────────────────────────────────────────────────────── */

#define Z80_WRITE_BYTE(address, x)                                      \
{                                                                       \
    z80ctx_mem_write((struct Z80CpuContext *)(context),                \
                     (uint16_t)((address) & 0xffff),                   \
                     (uint8_t)((x) & 0xff));                           \
}

#define Z80_WRITE_WORD(address, x)                                      \
{                                                                       \
    z80ctx_mem_write((struct Z80CpuContext *)(context),                \
                     (uint16_t)((address) & 0xffff),                   \
                     (uint8_t)((x) & 0xff));                           \
    z80ctx_mem_write((struct Z80CpuContext *)(context),                \
                     (uint16_t)(((address) + 1) & 0xffff),             \
                     (uint8_t)(((unsigned)(x) >> 8) & 0xff));          \
}

/* Interrupt vector table reads — use the same memory path */
#define Z80_READ_WORD_INTERRUPT(address, x)   Z80_READ_WORD((address), (x))
#define Z80_WRITE_WORD_INTERRUPT(address, x)  Z80_WRITE_WORD((address), (x))

/* ── I/O ────────────────────────────────────────────────────────────────── */

/* libz80 passes only the low byte (C register) for IN A,(C) and OUT (C),x.
 * Reconstruct the full 16-bit port by supplying B from the Z80 state so that
 * row-selecting keyboard reads (e.g. IN A,(C) with BC=$FEFE) work correctly.
 * z80ctx_b_reg() is a thin C helper defined in z80_cpu.cpp that reads the B
 * register from the context without exposing Z80CpuContext's layout here.
 */
#define Z80_INPUT_BYTE(port, x)                                                 \
{                                                                               \
    uint16_t _full_port = (uint16_t)(                                           \
        ((uint16_t)z80ctx_b_reg((struct Z80CpuContext *)(context)) << 8)        \
        | ((port) & 0x00FF));                                                   \
    (x) = z80ctx_io_in((struct Z80CpuContext *)(context), _full_port);          \
}

#define Z80_OUTPUT_BYTE(port, x)                                                \
{                                                                               \
    uint16_t _full_port = (uint16_t)(                                           \
        ((uint16_t)z80ctx_b_reg((struct Z80CpuContext *)(context)) << 8)        \
        | ((port) & 0x00FF));                                                   \
    z80ctx_io_out((struct Z80CpuContext *)(context),                            \
                  _full_port,                                                    \
                  (uint8_t)((x) & 0xff));                                       \
}

/* ── C helper function declarations ─────────────────────────────────────── */
/* Implemented in z80_cpu.cpp; declared here with C linkage so z80emu.c
 * (plain C) can call them through the macros.
 */

extern uint8_t  z80ctx_mem_read (struct Z80CpuContext *ctx, uint16_t addr);
extern void     z80ctx_mem_write(struct Z80CpuContext *ctx, uint16_t addr, uint8_t val);
extern uint8_t  z80ctx_io_in    (struct Z80CpuContext *ctx, uint16_t port);
extern void     z80ctx_io_out   (struct Z80CpuContext *ctx, uint16_t port, uint8_t val);
extern uint8_t  z80ctx_b_reg    (struct Z80CpuContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __Z80USER_INCLUDED__ */
