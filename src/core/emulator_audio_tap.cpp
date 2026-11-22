// GH #531 -- the cold bodies of the audio write tap. They live in a translation
// unit of their own so that emulator.cpp grows only by the gate tests and the
// calls: a body added to a hot TU can shift inlining there. See
// Emulator::set_audio_tap() in emulator.h.
//
// Every function returns at once while a rewind replays frames (`replay_mode_`):
// those writes already happened and were logged the first time.

#include "core/emulator.h"

using jnext::dbg::AudioDropReason;
using jnext::dbg::AudioLogEntry;
using jnext::dbg::AudioMarker;
using jnext::dbg::AudioTarget;

AudioLogEntry Emulator::tap_entry_() const
{
    AudioLogEntry e;
    e.cycle = clock_.get();
    stamp_at_(e.cycle, e.frame, e.vc, e.hc);
    // MachineInfo::tstates_per_line is at the 3.5 MHz reference, and master cycles
    // are T-states x 8; equal to vc * tstates_per_line + hc / 2.
    e.tstate_in_frame = static_cast<uint32_t>(
        (e.cycle - frame_cycle_) / (timing_.master_cycles_per_line / uint64_t(timing_.tstates_per_line)));
    e.pc = debug_slot_pc_;
    e.source = audio_write_source_();
    return e;
}

// A marker carries only the timestamp fields.
AudioLogEntry Emulator::marker_entry_(AudioMarker m) const
{
    AudioLogEntry e = tap_entry_();
    e.pc = 0;
    e.source = jnext::dbg::EventSource::Cpu;
    e.marker = m;
    return e;
}

void Emulator::tap_ay_data_(uint16_t port, uint8_t val, uint8_t prev, uint8_t chip, uint8_t addr)
{
    if (replay_mode_ || !audio_tap_) return;
    AudioLogEntry e = tap_entry_();
    e.target = AudioTarget::AyRegister;
    e.chip = chip;
    e.reg = addr;
    e.value = val;
    e.prev = prev;
    e.port = port;
    // Any APPLIED write to R13 restarts the envelope, whatever the value
    // (ym2149.vhd:188,207-210).
    if (addr == 13) e.flags |= jnext::dbg::AudioFlag::EnvelopeRestart;
    audio_tap_->audio_record(e);
}

void Emulator::tap_ay_select_(uint16_t port, uint8_t val, uint8_t prev_chip)
{
    if (replay_mode_ || !audio_tap_) return;
    AudioLogEntry e = tap_entry_();
    e.target = AudioTarget::AyChipSelect;
    e.chip = uint8_t(turbosound_.selected_index());
    e.value = val;
    e.prev = prev_chip;
    e.port = port;
    audio_tap_->audio_record(e);
}

void Emulator::tap_dac_(int ch, uint8_t val, uint16_t port, AudioTarget t, uint8_t reg)
{
    if (replay_mode_ || !audio_tap_) return;
    AudioLogEntry e = tap_entry_();
    e.target = t;
    e.chip = uint8_t(ch);
    e.reg = reg;
    e.value = val;
    e.prev = dac_.channel(ch);
    e.port = port;
    audio_tap_->audio_record(e);
}

void Emulator::tap_beeper_(uint16_t port, uint8_t val)
{
    if (replay_mode_ || !audio_tap_) return;
    const bool ear = (val >> 4) & 1, mic = (val >> 3) & 1;
    // A border-only write, or a repeat, changes neither line: nothing to log.
    if (ear == beeper_.ear() && mic == beeper_.mic()) return;
    AudioLogEntry e = tap_entry_();
    e.target = AudioTarget::Beeper;
    e.value = val;
    e.prev = uint8_t((beeper_.ear() ? 0x10 : 0) | (beeper_.mic() ? 0x08 : 0));
    e.port = port;
    audio_tap_->audio_record(e);
}

void Emulator::tap_drop_(AudioDropReason r, uint16_t port, uint8_t val)
{
    if (replay_mode_ || !audio_tap_) return;
    AudioLogEntry e = tap_entry_();
    e.value = val;
    e.port = port;
    e.flags = jnext::dbg::AudioFlag::Dropped;
    e.drop_reason = r;
    switch (r) {
        case AudioDropReason::DacFamilyDisabled:
        case AudioDropReason::DacDisabled:
            e.target = AudioTarget::Dac;
            e.chip = 0xFF;      // a dropped write reached no channel
            break;
        case AudioDropReason::DacMirrorDisabled:
            e.target = AudioTarget::DacMirror;
            e.chip = 0xFF;
            e.reg = uint8_t(port & 0xFF);
            break;
        case AudioDropReason::None:
        case AudioDropReason::Count:
            return;
        default: {
            // An AY reason. Port bit 14 set is 0xFFFD (A15:14 = 11); clear is 0xBFFD / 0xBFF5.
            const uint8_t chip = uint8_t(turbosound_.selected_index());
            e.chip = chip;
            if (port & 0x4000) {
                e.target = AudioTarget::AyChipSelect;
                e.prev = chip;
            } else {
                const uint8_t addr = turbosound_.ay(chip).address();
                e.target = AudioTarget::AyRegister;
                e.reg = addr;
                e.prev = addr < 16 ? turbosound_.ay(chip).read_register(addr) : 0;
            }
            break;
        }
    }
    audio_tap_->audio_record(e);
}

void Emulator::tap_marker_(AudioMarker m)
{
    if (replay_mode_ || !audio_tap_) return;
    AudioLogEntry e = marker_entry_(m);
    audio_tap_->audio_record(e);
}

void Emulator::tap_rewind_landing_()
{
    if (!audio_tap_) return;
    audio_tap_->audio_rewind_landing(marker_entry_(AudioMarker::Rewind));
}
