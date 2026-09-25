#include "audio/dac.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

Dac::Dac() { reset(); }

void Dac::reset()
{
    // VHDL: reset to 0x80 (midpoint for unsigned 8-bit audio = silence)
    ch_[0] = 0x80;
    ch_[1] = 0x80;
    ch_[2] = 0x80;
    ch_[3] = 0x80;
}

void Dac::write_channel(int ch, uint8_t val)
{
    if (ch < 0 || ch >= 4) return;
    ch_[ch] = val;
    if (write_callback_) write_callback_(ch, val);
}

void Dac::write_mono(uint8_t val)
{
    write_channel(0, val);
    write_channel(3, val);
}

void Dac::write_left(uint8_t val)
{
    write_channel(1, val);
}

void Dac::write_right(uint8_t val)
{
    write_channel(2, val);
}

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 4 bytes as block 21 of the 2 292 965-byte stream.
//
// ONE `d.bytes` and not four `d.u8`s, because that is what the stream
// carries: the hand-written pair wrote `write_bytes(ch_, 4)`, a single
// 4-byte run, and a `.jns` therefore renders it as one 8-character hex
// string. Its length comes from the DECLARATION, so no count in the stream
// can size the write. (Four scalars would be byte-identical here — the
// choice is about the JSON shape, which is what files are made of.)
//
// The four channels are A, B, C, D; the mixer sums A+B to the left and C+D
// to the right (dac.h:37-38, soundrive.vhd:112-113).
void Dac::describe_state(jnext::save::StateDesc& d)
{
    d.bytes("channels", ch_, 4);
}

void Dac::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Dac::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
}
