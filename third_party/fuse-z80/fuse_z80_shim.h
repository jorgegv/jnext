/* fuse_z80_shim.h — Adapter layer replacing FUSE emulator dependencies.
 *
 * The FUSE Z80 core expects libspectrum types, global tstates, memory/IO
 * functions, and contention macros.  This header provides all of them so that
 * the opcode files (opcodes_base.c, z80_cb.c, z80_ed.c, z80_ddfd.c,
 * z80_ddfdcb.c) can compile without any other FUSE header.
 *
 * Copyright (c) 2026 Jorge Gonzalez Villalonga — GPLv2+ (matching FUSE licence)
 */

#ifndef FUSE_Z80_SHIM_H
#define FUSE_Z80_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── libspectrum type replacements ─────────────────────────────────────── */

typedef uint8_t  libspectrum_byte;
typedef uint16_t libspectrum_word;
typedef uint32_t libspectrum_dword;
typedef int8_t   libspectrum_signed_byte;
typedef int32_t  libspectrum_signed_dword;

/* ── Processor state (from FUSE z80.h) ─────────────────────────────────── */

typedef union {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct { libspectrum_byte h, l; } b;
#else
    struct { libspectrum_byte l, h; } b;
#endif
    libspectrum_word w;
} regpair;

typedef struct {
    regpair af, bc, de, hl;
    regpair af_, bc_, de_, hl_;
    regpair ix, iy;
    libspectrum_byte i;
    libspectrum_word r;     /* low 7 bits of R */
    libspectrum_byte r7;    /* high bit of R */
    regpair sp, pc;
    regpair memptr;         /* hidden WZ register */
    int iff2_read;
    libspectrum_byte iff1, iff2, im;
    int halted;
    libspectrum_byte q;     /* internal F-assembly register */
    libspectrum_signed_dword interrupts_enabled_at;
} processor;

/* The global CPU state instance */
extern processor z80;

/* ── Flag / lookup tables ──────────────────────────────────────────────── */

extern const libspectrum_byte halfcarry_add_table[];
extern const libspectrum_byte halfcarry_sub_table[];
extern const libspectrum_byte overflow_add_table[];
extern const libspectrum_byte overflow_sub_table[];
extern libspectrum_byte sz53_table[];
extern libspectrum_byte sz53p_table[];
extern libspectrum_byte parity_table[];

/* ── Cycle counter ─────────────────────────────────────────────────────── */

/* Global T-state counter; the execution loop runs until tstates >= event_next_event */
extern libspectrum_dword tstates;
extern libspectrum_dword event_next_event;

/* ── Contention data structures (used by z80_macros.h contend_* macros) ── */

/* Memory pages: 8 KB each (2^13), giving 8 pages for 64 KB address space. */
#define MEMORY_PAGE_SIZE_LOGARITHM 13

typedef struct {
    int contended;   /* non-zero if this page is contended */
} memory_page_entry_t;

/* Per-page contention flags — set by emulator init for the current machine type */
extern memory_page_entry_t memory_map_read[8];
extern memory_page_entry_t memory_map_write[8];

/* Contention delay lookup tables indexed by T-state position in frame.
 * ula_contention[t] = extra T-states of delay at position t.
 * Size must accommodate the largest frame (Pentagon: 71680 T-states) + margin. */
#define ULA_CONTENTION_TABLE_SIZE 80000
extern libspectrum_dword ula_contention[ULA_CONTENTION_TABLE_SIZE];
extern libspectrum_dword ula_contention_no_mreq[ULA_CONTENTION_TABLE_SIZE];

/* ── Memory / IO access callbacks ──────────────────────────────────────── */

/* These are implemented by the Z80Cpu wrapper (z80_cpu.cpp) */
extern libspectrum_byte fuse_z80_readbyte_raw(libspectrum_word address); /* no timing */
extern libspectrum_byte fuse_z80_readbyte(libspectrum_word address);     /* +3T +contention */
extern void             fuse_z80_writebyte(libspectrum_word address, libspectrum_byte b); /* +3T +contention */
extern libspectrum_byte fuse_z80_readport(libspectrum_word port);
extern void             fuse_z80_writeport(libspectrum_word port, libspectrum_byte b);

/* RZX playback offset — not used */
extern int rzx_instructions_offset;

/* FUSE event IDs — not used, but z80_ed.c references them */
extern int z80_interrupt_event;
extern int z80_nmos_iff2_event;

/* tape_save_trap / tape_load_trap — no tape trapping */
static inline int tape_save_trap(void) { return -1; }
static inline int tape_load_trap(void) { return -1; }

/* slt_trap — SLT snapshot trap, not used */
static inline void slt_trap(libspectrum_word hl, libspectrum_byte a) { (void)hl; (void)a; }

/* z80_retn — RETN peripheral hook (spectranet); not needed */
static inline void z80_retn(void) {}

/* event_add — FUSE event scheduler stub */
static inline void event_add(libspectrum_dword ts, int type) { (void)ts; (void)type; }

/* ── Core API ──────────────────────────────────────────────────────────── */

void fuse_z80_init_tables(void);
void fuse_z80_reset(int hard_reset);
int  fuse_z80_execute_one(void);   /* execute one instruction, return T-states */
int  fuse_z80_interrupt(libspectrum_byte vector);
void fuse_z80_nmi(void);

#ifdef __cplusplus
}
#endif

#endif /* FUSE_Z80_SHIM_H */
