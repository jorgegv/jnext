// GH #531 -- the audio write log's CSV format, in one place. `Debugger::audio_log_export()`,
// the Qt Log tab's Export and the `--audio-log` stream all call it, so the three
// cannot disagree. PUBLISHED: it includes `debug/events.h` only.
//
// Header (one line, LF-terminated rows on every host):
//   frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port
// Decimal: frame, line (= vc), hc, tstate (= tstate_in_frame), cycle, chip, reg, flags.
// Hex, uppercase: pc and port as 0x%04X, value and prev as 0x%02X.
// source: cpu dma copper debugger. target: ay ay_select dac dac_mirror beeper.
// A chip of 0xFF (a dropped DAC or mirror write) is an empty field.
// A marker row has 15 fields too: the five timestamp fields, an empty pc, the word
// `marker` in `source`, the kind (reset, load, rewind) in `target`, the rest empty.
#pragma once

#include <string>

#include "debug/events.h"

namespace jnext {
namespace dbg {

/// The header line, with its trailing LF.
std::string audio_log_csv_header();

/// One row, with its trailing LF, appended to `out`.
void audio_log_csv_append(std::string& out, const AudioLogEntry& e);

/// The reason's CSV token (`none`, `ay_port_disabled`, ...).
const char* audio_log_reason_token(AudioDropReason r);

}  // namespace dbg
}  // namespace jnext
