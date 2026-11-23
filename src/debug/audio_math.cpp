#include "debug/audio_math.h"

#include <algorithm>
#include <cmath>

double psg_tone_hz(uint16_t tone_period, uint32_t clock_hz)
{
    const unsigned p = std::max<unsigned>(tone_period & 0x0FFF, 1);
    return static_cast<double>(clock_hz) / (16.0 * p);
}

double psg_envelope_hz(uint16_t envelope_period, uint8_t shape, uint32_t clock_hz)
{
    const unsigned p = std::max<unsigned>(envelope_period, 1);
    switch (shape & 0x0F) {
        case 8:  case 12: return static_cast<double>(clock_hz) / (256.0 * p);
        case 10: case 14: return static_cast<double>(clock_hz) / (512.0 * p);
        default:          return 0.0;
    }
}

PsgNote psg_note(double hz)
{
    PsgNote n;
    if (!(hz > 0.0) || !std::isfinite(hz)) return n;

    const double m = 69.0 + 12.0 * std::log2(hz / 440.0);
    // 1e-9: a tie must go to the upper note whatever the libm's rounding of log2.
    const double nearest = std::floor(m + 0.5 + 1e-9);
    if (nearest < 0.0 || nearest > 127.0) {
        // Out of the MIDI range: not valid, but say which note number it is when
        // it fits a byte (MIDI 128 is the first above the top).
        if (nearest > 127.0 && nearest <= 255.0) n.midi = static_cast<uint8_t>(nearest);
        return n;
    }

    static const char* const kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int midi = static_cast<int>(nearest);
    n.valid = true;
    n.midi = static_cast<uint8_t>(midi);
    const char* nm = kNames[midi % 12];
    std::size_t i = 0;
    for (; nm[i] != '\0'; ++i) n.name[i] = nm[i];
    n.name[i] = '\0';
    n.octave = static_cast<int8_t>(midi / 12 - 1);
    n.cents = 100.0 * (m - nearest);
    return n;
}

const char* psg_envelope_glyph(uint8_t shape)
{
    static const char* const kGlyphs[16] = {
        "\\___", "\\___", "\\___", "\\___",    // 0-3: C = 0, attack 0
        "/___",  "/___",  "/___",  "/___",     // 4-7: C = 0, attack 1
        "\\\\\\\\",                             // 8
        "\\___",                                // 9
        "\\/\\/",                               // 10
        "\\'''",                                // 11
        "////",                                 // 12
        "/'''",                                 // 13
        "/\\/\\",                               // 14
        "/___",                                 // 15
    };
    return kGlyphs[shape & 0x0F];
}

ScopeTrigger scope_trigger(const int16_t* s, size_t n, size_t window)
{
    ScopeTrigger t;
    if (n == 0) return t;
    int lo = s[0], hi = s[0];
    for (size_t i = 1; i < n; ++i) {
        lo = std::min<int>(lo, s[i]);
        hi = std::max<int>(hi, s[i]);
    }
    t.level = (lo + hi + 1) / 2;
    if (n < window) return t;
    t.start = n - window;
    if (lo == hi) return t;
    for (size_t i = n - window; i >= 1; --i) {
        if (s[i - 1] < t.level && s[i] >= t.level) {
            t.triggered = true;
            t.start = i;
            break;
        }
    }
    return t;
}

ScopeMeter scope_meter(const int16_t* s, size_t n, int rest)
{
    ScopeMeter m;
    if (n == 0) return m;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const int d = static_cast<int>(s[i]) - rest;
        m.peak = std::max(m.peak, d < 0 ? -d : d);
        sum += static_cast<double>(d) * d;
    }
    m.rms = std::sqrt(sum / static_cast<double>(n));
    return m;
}
