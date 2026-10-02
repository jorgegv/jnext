#pragma once

// ---------------------------------------------------------------------------
// GH #26 WP6 / #20 — what a replay script's header says about the session it
// was recorded in (`script::RecordingInfo`), read from the machine the loop
// owner booted. One definition for the three loop owners, which hand it to
// `ScriptHost::set_recording_info()`.
//
// `config().load_file` is the CURRENT boot's program: every cold boot — the
// first one, a menu load, a hard reset (which loads nothing) — rebuilds the
// machine from a config carrying it (emulator_boot.h). The SD identity is the
// `.jns` Tier 1 (size, volume id, partition table), read without the
// whole-image digest: the header only describes the card, it compares nothing.
// ---------------------------------------------------------------------------

#include <ctime>
#include <string>

#include "core/emulator.h"
#include "core/sd_snapshot_identity.h"
#include "script/recorder.h"

inline jnext::script::RecordingInfo recording_info_of(const Emulator& emu) {
    jnext::script::RecordingInfo info;
    const EmulatorConfig& c = emu.config();
    info.load_file = c.load_file;
    if (c.rtc_fixed) {
        char buf[32];
        std::tm tm = c.rtc_fixed_tm;
        if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm) > 0) info.rtc = buf;
    }
    info.sd_image = c.sd_card_image;
    if (!info.sd_image.empty()) {
        jnext::jns::SdCardInfo sd;
        std::string            why;
        if (jnext::describe_sdcard_for_snapshot(info.sd_image, /*read_only=*/true, sd, why,
                                                /*want_content_stamp=*/false))
            info.sd_id = sd.identity.describe();
    }
    return info;
}
