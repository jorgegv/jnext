#include "audio/mixer.h"
#include "audio/i2s.h"
#include <algorithm>

Mixer::Mixer() : buffer_(RING_BUFFER_SIZE * 2, 0) { reset(); }

void Mixer::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0);
    write_pos_ = 0;
    read_pos_ = 0;
    count_ = 0;
    // VHDL audio_mixer.vhd: exc_i is a combinational input that follows
    // beep_spkr_excl (zxnext.vhd:6504). Default both NR 0x06 b6 and NR 0x08
    // b4 cleared at power-on means exc_i='0'. Emulator::reset() restores it
    // via set_exc_i(beep_spkr_excl()) once NR 0x08 has settled to 0x10.
    exc_i_ = false;
}

void Mixer::generate_sample(const Beeper& beeper, const TurboSound& ts, const Dac& dac)
{
    // VHDL audio_mixer.vhd scaling (all values are 13-bit unsigned, 0-8191):
    //
    //   ear = 0x200 (512) when ear=1, else 0  (includes tape EAR input)
    //   mic = 0x080 (128) when mic=1, else 0
    //   ay_L = 0 & ay_L_i  (12-bit → 13-bit, range 0-2295)
    //   ay_R = 0 & ay_R_i
    //   dac_L = 00 & dac_L_i & 00  (9-bit × 4, range 0-2040)
    //   dac_R = 00 & dac_R_i & 00
    //   pcm_L = ear + mic + ay_L + dac_L  (range 0-5998)
    //   pcm_R = ear + mic + ay_R + dac_R

    uint16_t ear = beeper.ear() ? 512u : 0u;
    uint16_t mic = beeper.mic() ? 128u : 0u;
    uint16_t tape_ear = beeper.tape_ear() ? 512u : 0u;

    // VHDL audio_mixer.vhd:80-81 — when exc_i='1' (speaker-exclusive mode,
    // beep_spkr_excl from zxnext.vhd:6504) the ear/mic muxes are gated to
    // (others=>'0'). The tape-EAR contribution rides the same i_AUDIO_EAR
    // wire upstream of the mux, so it is silenced by the same gate.
    if (exc_i_) {
        ear = 0;
        mic = 0;
        tape_ear = 0;
    }

    uint16_t ay_L = ts.pcm_left();    // 12-bit, already correct scale
    uint16_t ay_R = ts.pcm_right();

    uint16_t dac_L = dac.pcm_left() << 2;   // 9-bit × 4
    uint16_t dac_R = dac.pcm_right() << 2;

    // Pi I2S term: mirrors audio_mixer.vhd:89-90 (10-bit zero-extended
    // to 13-bit). Mixer does not own the I2s; Emulator wires it via
    // set_i2s_source(). When unwired, the term is 0 (silence).
    //
    // G73: consume the GATED pi_audio_L/R per VHDL zxnext.vhd:2358-2359
    // — the same signals that feed audio_mixer.vhd's pi_i2s_L_i/R_i
    // ports (zxnext.vhd:6505-6506). With NR 0xA2 disabled or muted,
    // pi_audio_L/R is the 10-bit DC midpoint 0x200 ("silence"). The raw
    // sample latch (left()/right()) does NOT enter the mixer path.
    uint16_t i2s_L = i2s_ ? i2s_->pi_audio_L() : 0u;   // 0..1023
    uint16_t i2s_R = i2s_ ? i2s_->pi_audio_R() : 0u;

    // audio_mixer.vhd:99-100 13-bit sum — every term is in the 13-bit
    // domain; uint16_t is a superset of 13-bit-unsigned so no clamping
    // is needed at this point.
    uint16_t pcm_L = ear + mic + tape_ear + ay_L + dac_L + i2s_L;
    uint16_t pcm_R = ear + mic + tape_ear + ay_R + dac_R + i2s_R;

    // Convert to signed 16-bit.  Center at the resting DC level so that
    // silence produces 0.  At rest: DAC = (0x80+0x80)<<2 = 1024 per channel,
    // all other sources = 0, so resting level is 1024.
    // The real hardware has AC-coupled output (capacitor blocks DC); we
    // replicate that by subtracting the resting level instead of the 13-bit
    // midpoint.  Scale by 4 to use more of the int16 dynamic range.
    constexpr int32_t DC_REST = 1024;  // DAC silence level in 13-bit space
    int32_t sL = (static_cast<int32_t>(pcm_L) - DC_REST) * 4;
    int32_t sR = (static_cast<int32_t>(pcm_R) - DC_REST) * 4;

    // Clamp to int16_t range
    sL = std::clamp(sL, static_cast<int32_t>(-32768), static_cast<int32_t>(32767));
    sR = std::clamp(sR, static_cast<int32_t>(-32768), static_cast<int32_t>(32767));

    // Write to ring buffer (drop oldest sample if full)
    if (count_ >= RING_BUFFER_SIZE) {
        // Overrun: advance read position
        read_pos_ = (read_pos_ + 2) % (RING_BUFFER_SIZE * 2);
        count_--;
    }

    buffer_[write_pos_]     = static_cast<int16_t>(sL);
    buffer_[write_pos_ + 1] = static_cast<int16_t>(sR);
    write_pos_ = (write_pos_ + 2) % (RING_BUFFER_SIZE * 2);
    count_++;

    // Notify the recording callback (if any) with this sample pair.
    if (record_callback_) {
        int16_t pair[2] = { static_cast<int16_t>(sL), static_cast<int16_t>(sR) };
        record_callback_(pair, 1);
    }
}

int Mixer::available() const { return count_; }

int Mixer::read_samples(int16_t* out, int count)
{
    int n = std::min(count, count_);
    for (int i = 0; i < n; i++) {
        out[i * 2]     = buffer_[read_pos_];
        out[i * 2 + 1] = buffer_[read_pos_ + 1];
        read_pos_ = (read_pos_ + 2) % (RING_BUFFER_SIZE * 2);
    }
    count_ -= n;
    return n;
}
