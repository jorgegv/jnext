#include "debug/audio_log_csv.h"

#include <cstdio>

namespace jnext {
namespace dbg {

std::string audio_log_csv_header()
{
    return "frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port\n";
}

const char* audio_log_reason_token(AudioDropReason r)
{
    switch (r) {
        case AudioDropReason::None:                 return "none";
        case AudioDropReason::AyPortDisabled:       return "ay_port_disabled";
        case AudioDropReason::AyHeldInReset:        return "ay_held_in_reset";
        case AudioDropReason::AyChipSelectTurboOff: return "ay_chip_select_turbo_off";
        case AudioDropReason::AySelectIgnored:      return "ay_select_ignored";
        case AudioDropReason::AyAddressHigh:        return "ay_address_high";
        case AudioDropReason::DacFamilyDisabled:    return "dac_family_disabled";
        case AudioDropReason::DacDisabled:          return "dac_disabled";
        case AudioDropReason::DacMirrorDisabled:    return "dac_mirror_disabled";
        case AudioDropReason::Count:                break;
    }
    return "none";
}

namespace {

const char* source_token(EventSource s)
{
    switch (s) {
        case EventSource::Cpu:      return "cpu";
        case EventSource::Copper:   return "copper";
        case EventSource::Dma:      return "dma";
        case EventSource::Debugger: return "debugger";
        case EventSource::Any:      break;
    }
    return "cpu";
}

const char* target_token(AudioTarget t)
{
    switch (t) {
        case AudioTarget::AyRegister:   return "ay";
        case AudioTarget::AyChipSelect: return "ay_select";
        case AudioTarget::Dac:          return "dac";
        case AudioTarget::DacMirror:    return "dac_mirror";
        case AudioTarget::Beeper:       return "beeper";
        case AudioTarget::Count:        break;
    }
    return "ay";
}

const char* marker_token(AudioMarker m)
{
    switch (m) {
        case AudioMarker::Reset:     return "reset";
        case AudioMarker::StateLoad: return "load";
        case AudioMarker::Rewind:    return "rewind";
        case AudioMarker::None:      break;
    }
    return "";
}

}  // namespace

void audio_log_csv_append(std::string& out, const AudioLogEntry& e)
{
    char buf[192];
    int n;
    if (e.marker != AudioMarker::None) {
        n = std::snprintf(buf, sizeof buf, "%u,%d,%d,%u,%llu,,marker,%s,,,,,,,\n",
                          unsigned(e.frame), int(e.vc), int(e.hc), unsigned(e.tstate_in_frame),
                          static_cast<unsigned long long>(e.cycle), marker_token(e.marker));
    } else {
        char chip[8] = "";
        if (e.chip != 0xFF) std::snprintf(chip, sizeof chip, "%u", unsigned(e.chip));
        n = std::snprintf(buf, sizeof buf,
                          "%u,%d,%d,%u,%llu,0x%04X,%s,%s,%s,%u,0x%02X,0x%02X,%u,%s,0x%04X\n",
                          unsigned(e.frame), int(e.vc), int(e.hc), unsigned(e.tstate_in_frame),
                          static_cast<unsigned long long>(e.cycle), unsigned(e.pc),
                          source_token(e.source), target_token(e.target), chip,
                          unsigned(e.reg), unsigned(e.value), unsigned(e.prev), unsigned(e.flags),
                          audio_log_reason_token(e.drop_reason), unsigned(e.port));
    }
    if (n > 0) out.append(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1);
}

}  // namespace dbg
}  // namespace jnext
