#include "audio/mixer.h"
#include <algorithm>

Mixer::Mixer() : buffer_(RING_BUFFER_SIZE * 2, 0) { reset(); }

void Mixer::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0);
    write_pos_ = 0;
    read_pos_ = 0;
    count_ = 0;
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

    uint16_t ay_L = ts.pcm_left();    // 12-bit, already correct scale
    uint16_t ay_R = ts.pcm_right();

    uint16_t dac_L = dac.pcm_left() << 2;   // 9-bit × 4
    uint16_t dac_R = dac.pcm_right() << 2;

    uint16_t pcm_L = ear + mic + tape_ear + ay_L + dac_L;
    uint16_t pcm_R = ear + mic + tape_ear + ay_R + dac_R;

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
