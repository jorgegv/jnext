#pragma once

// ---------------------------------------------------------------------------
// Audio introspection arithmetic, for the debugger's Audio panel and any other
// frontend: the pitch of an AY/YM tone or envelope, as a note and cents, and
// the envelope shape glyph. Standard headers only, so it joins the published
// debug surface (test/lint-debug-headers.sh).
//
// The frequencies are those of the FPGA's ym2149 at the PSG clock-enable rate
// the caller passes (1.75 MHz on the Next, `AudioState::psg_clock_hz`): never a
// literal here, because the 128K machine's 1.7734 MHz clock plays the same
// period about 23 cents higher.
// ---------------------------------------------------------------------------

#include <cstdint>

/// Tone frequency in Hz of a 12-bit tone period. The tone counter toggles every
/// `max(period, 1)` divider ticks and the divider takes one tick per 8 PSG
/// clock enables (`ym2149.vhd:306-325`, I_SEL_L = 1 at `turbosound.vhd:164`),
/// so f = clock / (16 * period). Period 0 behaves as 1 (:306).
double psg_tone_hz(uint16_t tone_period, uint32_t clock_hz);

/// Envelope repetition frequency in Hz for a 16-bit envelope period and an R13
/// shape (low 4 bits used). Only the shapes that repeat have one: 8 and 12
/// (sawtooth, 32 steps per cycle) and 10 and 14 (triangle, 64 steps per cycle,
/// counting the one-step dwell at each rail); every other shape is a one-shot
/// and returns 0 (`ym2149.vhd:334-350,402-462`). Period 0 behaves as 1.
double psg_envelope_hz(uint16_t envelope_period, uint8_t shape, uint32_t clock_hz);

/// A frequency named as the nearest equal-tempered note (A4 = 440 Hz).
struct PsgNote {
    bool valid = false;   ///< false: not a finite positive frequency, or outside MIDI 0..127
    uint8_t midi = 0;     ///< the nearest note number: 0..127 when valid; 128..255 above the range; 0 below it or for a bad frequency
    char name[4] = {0};   ///< "C", "C#", "D", ... sharps only, NUL terminated; empty when invalid
    int8_t octave = 0;    ///< scientific pitch notation: midi / 12 - 1 (C4 = 60)
    double cents = 0.0;   ///< offset from the named note, -50 (included) up to +50 (excluded)
};

/// Nearest note to `hz`. A tie between two notes names the upper one, and the
/// choice does not depend on the host's libm (a computed exact tie lands within
/// an ulp of 0.5 on glibc, MinGW and Emscripten alike).
PsgNote psg_note(double hz);

/// The envelope shape as four ASCII characters, from the table at
/// `ym2149.vhd:371-391` (`\` falling, `/` rising, `'` held high, `_` held
/// low). The low 4 bits of `shape` are used.
const char* psg_envelope_glyph(uint8_t shape);
